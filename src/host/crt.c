/*
 * CRT filter: a small runner for RetroArch-style GLSL presets (.glslp), used
 * for the Lola Retro shaders (third_party/lola-retro, embedded at build
 * time). Each pass is a full-screen quad into a framebuffer; the last pass
 * draws into an SDL target texture, which SDL then puts on screen like the
 * plain picture, so the menus and overlays are drawn on top as before.
 *
 * The GL calls go between SDL's: SDL_FlushRenderer() first (SDL then sets
 * its own state again from scratch), and the few bindings SDL doesn't track
 * (framebuffer, vertex array, buffer) are put back afterwards.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <string.h>
#include "crt.h"

const char *embedded_file_find(const char *path, size_t *size);   /* lola_retro_files.c */

const char *const crt_names[CRT_PRESET_COUNT] = {
    "Off", "Consumer TV (Composite)", "Consumer TV (RGB SCART)", "Trinitron TV (S-Video)",
    "Old TV (RF Antenna)", "Studio Monitor (Composite)", "Studio Monitor (RGB)",
    "Commodore 1084S Monitor", "CRT Projector",
};
static const char *const preset_file[CRT_PRESET_COUNT] = {
    NULL, "consumer-tv-composite", "consumer-tv-scart-rgb", "trinitron-s-video",
    "old-tv-rf", "studio-monitor-composite", "studio-monitor-rgb",
    "amiga-1084s", "crt-projector",
};

/* ---- GL entry points, all through SDL_GL_GetProcAddress ---- */
#define GL_FUNCS(X) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(void, GetIntegerv, (GLenum, GLint *)) X(GLboolean, IsEnabled, (GLenum)) \
    X(void, Enable, (GLenum)) X(void, Disable, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, Clear, (GLbitfield)) \
    X(void, GenTextures, (GLsizei, GLuint *)) X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, PixelStorei, (GLenum, GLint)) X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, ActiveTexture, (GLenum)) \
    X(GLuint, CreateShader, (GLenum)) X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteProgram, (GLuint)) X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) X(void, Uniform1f, (GLint, GLfloat)) X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) X(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
    X(void, BindVertexArray, (GLuint)) X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) X(void, Finish, (void)) \
    X(GLsync, FenceSync, (GLenum, GLbitfield)) X(GLenum, ClientWaitSync, (GLsync, GLbitfield, GLuint64)) \
    X(void, DeleteSync, (GLsync)) \
    X(void, GenQueries, (GLsizei, GLuint *)) X(void, QueryCounter, (GLuint, GLenum)) \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint *)) X(void, GetQueryObjectui64v, (GLuint, GLenum, GLuint64 *))
#define X(r, n, a) r (APIENTRY *n) a;
static struct { GL_FUNCS(X) } gl;
#undef X

static SDL_Renderer *R;
static int ok;
static char why[160] = "";
static GLint max_tex = 4096;

/* ---- the loaded preset ---- */
#define MAX_PASSES 12
#define MAX_PARAMS 160
enum { ST_SOURCE, ST_VIEWPORT, ST_ABSOLUTE };

typedef struct {
    char shader[96], alias[32];
    int linear, wrap, fp;          /* filter and wrap of this pass's input; float target */
    int stype[2];
    float scale[2];
    GLuint prog;
    GLint u_tex, u_orig, u_texsize, u_insize, u_outsize, u_origsize, u_originsize, u_frame, u_framedir, u_mvp;
    GLint u_alias[MAX_PASSES];     /* sampler of pass k's output, by its alias */
    GLint u_param[MAX_PARAMS];
    GLuint tex, fbo;               /* output (not used by the last pass) */
    int w, h;
} pass_t;

static struct {
    char key[128];                 /* preset loaded, or failed */
    int failed, n, np;
    pass_t p[MAX_PASSES];
    char pname[MAX_PARAMS][32];
    float pval[MAX_PARAMS];        /* what the passes get */
    float pdef[MAX_PARAMS];        /* the preset's values */
} P;

static GLuint in_tex, vbo, vao, out_fbo;
static int in_w, in_h;
static SDL_Texture *out_tex;
static int out_w, out_h;
static GLuint out_gl;
static GLenum out_target;
static GLsync fence;
/* GPU time of each pass, from GPU timestamps read a few frames later (no stall) */
#define NQ 4
static GLuint queries[NQ][MAX_PASSES + 1];
static int q_next, q_pending[NQ];           /* passes timed in that set (0: free) */
static float gpu_ms, pass_ms[MAX_PASSES];   /* smoothed */
static uint64_t gpu_ms_t;                  /* when gpu_ms was last measured */
static char look_desc[96];

const char *crt_why_not(void) { return why; }

float crt_gpu_ms(void) { return SDL_GetTicksNS() - gpu_ms_t < 1000000000ull ? gpu_ms : -1.0f; }

int crt_pass_times(char *buf, size_t n)
{
    size_t len = 0;
    buf[0] = 0;
    if (crt_gpu_ms() < 0.0f) return 0;
    for (int i = 0; i < P.n && len < n; i++) {
        const char *name = strrchr(P.p[i].shader, '/');
        name = name ? name + 1 : P.p[i].shader;
        int nl = (int)(strcspn(name, "."));
        len += SDL_snprintf(buf + len, n - len, "%s%.*s %.2f", i ? ", " : "", nl, name, pass_ms[i]);
    }
    return P.n;
}
const char *crt_describe(void) { return look_desc; }

int crt_init(SDL_Renderer *ren)
{
    R = ren;
    ok = 0;
    const char *name = SDL_GetRendererName(ren);
    if (!name || strcmp(name, "opengl")) {
        SDL_snprintf(why, sizeof why, "The CRT filter needs the OpenGL renderer (Display > Renderer); this one is %s.", name ? name : "unknown");
        return 0;
    }
#define X(r, n, a) if (!(*(void **)&gl.n = (void *)SDL_GL_GetProcAddress("gl" #n))) { SDL_snprintf(why, sizeof why, "The CRT filter needs OpenGL 3.3 (gl%s missing).", #n); return 0; }
    GL_FUNCS(X)
#undef X
    const char *v = (const char *)gl.GetString(GL_VERSION);
    int major = 0, minor = 0;
    if (v) SDL_sscanf(v, "%d.%d", &major, &minor);
    if (major * 10 + minor < 33) {
        SDL_snprintf(why, sizeof why, "The CRT filter needs OpenGL 3.3; this driver has %s.", v ? v : "an unknown version");
        return 0;
    }
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    SDL_Log("crt: OpenGL %s, %s, %s", v, (const char *)gl.GetString(GL_VENDOR), (const char *)gl.GetString(GL_RENDERER));

    why[0] = 0;
    ok = 1;
    return 1;
}

/* ---- preset text ---- */

/* "dir/../shaders/x.glsl" -> "shaders/x.glsl" */
static void join_path(char *out, size_t n, const char *base_file, const char *rel)
{
    char tmp[256];
    const char *slash = strrchr(base_file, '/');
    SDL_snprintf(tmp, sizeof tmp, "%.*s%s", slash ? (int)(slash - base_file + 1) : 0, base_file, rel);
    char *parts[32];
    int np = 0;
    for (char *s = strtok(tmp, "/"); s && np < 32; s = strtok(NULL, "/")) {
        if (!strcmp(s, "..")) { if (np) np--; }
        else if (strcmp(s, ".")) parts[np++] = s;
    }
    out[0] = 0;
    for (int i = 0; i < np; i++) {
        if (i) SDL_strlcat(out, "/", n);
        SDL_strlcat(out, parts[i], n);
    }
}

/* value of "key = value" in a preset (quotes removed); 0 if absent */
static int preset_get(const char *text, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        while (*l == ' ' || *l == '\t') l++;
        if (strncmp(l, key, kl)) continue;
        const char *p = l + kl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '"') p++;
        size_t i = 0;
        while (p[i] && p[i] != '\n' && p[i] != '\r' && p[i] != '"' && i + 1 < n) { out[i] = p[i]; i++; }
        while (i && (out[i - 1] == ' ' || out[i - 1] == '\t')) i--;
        out[i] = 0;
        return 1;
    }
    return 0;
}

static int param_index(const char *name, int add)
{
    for (int i = 0; i < P.np; i++) if (!strcmp(P.pname[i], name)) return i;
    if (!add || P.np >= MAX_PARAMS) return -1;
    SDL_strlcpy(P.pname[P.np], name, sizeof P.pname[0]);
    P.pval[P.np] = 0;
    return P.np++;
}

/* #pragma parameter NAME "label" default min max step */
static void scan_pragmas(const char *src)
{
    for (const char *l = strstr(src, "#pragma parameter"); l; l = strstr(l + 1, "#pragma parameter")) {
        char name[32];
        float def = 0;
        const char *q = strchr(l, '"'), *q2 = q ? strchr(q + 1, '"') : NULL;
        if (SDL_sscanf(l, "#pragma parameter %31s", name) != 1 || !q2) continue;
        if (SDL_sscanf(q2 + 1, "%f", &def) != 1) continue;
        int i = param_index(name, 1);
        if (i >= 0) P.pval[i] = def;
    }
}

static GLuint compile(GLenum type, const char *src, const char *what)
{
    const char *nl = strchr(src, '\n');
    int vlen = nl ? (int)(nl - src + 1) : 0;    /* the #version line stays first */
    const GLchar *s[3] = { src, type == GL_VERTEX_SHADER ? "#define VERTEX\n" : "#define FRAGMENT\n", src + vlen };
    GLint len[3] = { vlen, -1, -1 };
    GLuint sh = gl.CreateShader(type);
    gl.ShaderSource(sh, 3, s, len);
    gl.CompileShader(sh);
    GLint good = 0;
    gl.GetShaderiv(sh, GL_COMPILE_STATUS, &good);
    if (!good) {
        char log[1024] = "";
        gl.GetShaderInfoLog(sh, sizeof log, NULL, log);
        SDL_Log("crt: %s (%s) does not compile: %s", what, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static void free_preset(void)
{
    for (int i = 0; i < P.n; i++) {
        if (P.p[i].prog) gl.DeleteProgram(P.p[i].prog);
        if (P.p[i].tex) gl.DeleteTextures(1, &P.p[i].tex);
        if (P.p[i].fbo) gl.DeleteFramebuffers(1, &P.p[i].fbo);
    }
    P.n = P.np = 0;
    memset(P.p, 0, sizeof P.p);
}

static int load_preset(const char *path)
{
    free_preset();
    const char *text = embedded_file_find(path, NULL);
    char v[256];
    if (!text || !preset_get(text, "shaders", v, sizeof v)) { SDL_Log("crt: no preset %s", path); return 0; }
    P.n = SDL_atoi(v);
    if (P.n < 1 || P.n > MAX_PASSES) { SDL_Log("crt: %s: %d passes", path, P.n); P.n = 0; return 0; }
    for (int i = 0; i < P.n; i++) {
        pass_t *p = &P.p[i];
        char k[48];
        SDL_snprintf(k, sizeof k, "shader%d", i);
        if (!preset_get(text, k, v, sizeof v)) return 0;
        join_path(p->shader, sizeof p->shader, path, v);
        SDL_snprintf(k, sizeof k, "alias%d", i);
        if (!preset_get(text, k, p->alias, sizeof p->alias)) p->alias[0] = 0;
        SDL_snprintf(k, sizeof k, "filter_linear%d", i);
        p->linear = preset_get(text, k, v, sizeof v) && !strcmp(v, "true");
        SDL_snprintf(k, sizeof k, "float_framebuffer%d", i);
        p->fp = preset_get(text, k, v, sizeof v) && !strcmp(v, "true");
        SDL_snprintf(k, sizeof k, "wrap_mode%d", i);
        p->wrap = GL_CLAMP_TO_BORDER;
        if (preset_get(text, k, v, sizeof v)) {
            if (!strcmp(v, "clamp_to_edge")) p->wrap = GL_CLAMP_TO_EDGE;
            else if (!strcmp(v, "repeat")) p->wrap = GL_REPEAT;
            else if (!strcmp(v, "mirrored_repeat")) p->wrap = GL_MIRRORED_REPEAT;
        }
        int last = i == P.n - 1;
        for (int ax = 0; ax < 2; ax++) {
            p->stype[ax] = last ? ST_VIEWPORT : ST_SOURCE;
            p->scale[ax] = 1;
            const char *xy = ax ? "_y" : "_x";
            SDL_snprintf(k, sizeof k, "scale_type%s%d", xy, i);
            int have = preset_get(text, k, v, sizeof v);
            if (!have) { SDL_snprintf(k, sizeof k, "scale_type%d", i); have = preset_get(text, k, v, sizeof v); }
            if (have) p->stype[ax] = !strcmp(v, "viewport") ? ST_VIEWPORT : !strcmp(v, "absolute") ? ST_ABSOLUTE : ST_SOURCE;
            SDL_snprintf(k, sizeof k, "scale%s%d", xy, i);
            have = preset_get(text, k, v, sizeof v);
            if (!have) { SDL_snprintf(k, sizeof k, "scale%d", i); have = preset_get(text, k, v, sizeof v); }
            if (have) p->scale[ax] = (float)SDL_atof(v);
        }
    }
    /* parameters: the shaders' defaults, then the preset's values */
    for (int i = 0; i < P.n; i++) {
        const char *src = embedded_file_find(P.p[i].shader, NULL);
        if (!src) { SDL_Log("crt: missing %s", P.p[i].shader); return 0; }
        scan_pragmas(src);
    }
    if (preset_get(text, "parameters", v, sizeof v)) {
        char list[2048];
        preset_get(text, "parameters", list, sizeof list);
        for (char *s = strtok(list, ";"); s; s = strtok(NULL, ";")) {
            char val[64];
            int pi = param_index(s, 1);
            if (pi >= 0 && preset_get(text, s, val, sizeof val)) P.pval[pi] = (float)SDL_atof(val);
        }
    }
    /* programs */
    for (int i = 0; i < P.n; i++) {
        pass_t *p = &P.p[i];
        const char *src = embedded_file_find(p->shader, NULL);
        GLuint vs = compile(GL_VERTEX_SHADER, src, p->shader), fs = vs ? compile(GL_FRAGMENT_SHADER, src, p->shader) : 0;
        if (!fs) { if (vs) gl.DeleteShader(vs); return 0; }
        p->prog = gl.CreateProgram();
        gl.AttachShader(p->prog, vs);
        gl.AttachShader(p->prog, fs);
        gl.BindAttribLocation(p->prog, 0, "VertexCoord");
        gl.BindAttribLocation(p->prog, 1, "TexCoord");
        gl.LinkProgram(p->prog);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        GLint good = 0;
        gl.GetProgramiv(p->prog, GL_LINK_STATUS, &good);
        if (!good) {
            char log[1024] = "";
            gl.GetProgramInfoLog(p->prog, sizeof log, NULL, log);
            SDL_Log("crt: %s does not link: %s", p->shader, log);
            return 0;
        }
        GLuint g = p->prog;
        p->u_tex = gl.GetUniformLocation(g, "Texture");
        p->u_orig = gl.GetUniformLocation(g, "OrigTexture");
        p->u_texsize = gl.GetUniformLocation(g, "TextureSize");
        p->u_insize = gl.GetUniformLocation(g, "InputSize");
        p->u_outsize = gl.GetUniformLocation(g, "OutputSize");
        p->u_origsize = gl.GetUniformLocation(g, "OrigTextureSize");
        p->u_originsize = gl.GetUniformLocation(g, "OrigInputSize");
        p->u_frame = gl.GetUniformLocation(g, "FrameCount");
        p->u_framedir = gl.GetUniformLocation(g, "FrameDirection");
        p->u_mvp = gl.GetUniformLocation(g, "MVPMatrix");
        for (int k = 0; k < P.n; k++) {
            char n[48];
            SDL_snprintf(n, sizeof n, "%sTexture", P.p[k].alias);
            p->u_alias[k] = k < i && P.p[k].alias[0] ? gl.GetUniformLocation(g, n) : -1;
        }
        for (int k = 0; k < P.np; k++) p->u_param[k] = gl.GetUniformLocation(g, P.pname[k]);
    }
    return 1;
}

/* ---- drawing ---- */

static void sampler(GLuint tex, int linear, int wrap)
{
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

static void make_target(GLuint *tex, GLuint *fbo, int w, int h, int fp)
{
    if (!*tex) gl.GenTextures(1, tex);
    gl.BindTexture(GL_TEXTURE_2D, *tex);
    /* float_framebuffer: half floats, as RetroArch gives them (Vulkan, glcore) */
    gl.TexImage2D(GL_TEXTURE_2D, 0, fp ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, fp ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    sampler(*tex, 0, GL_CLAMP_TO_EDGE);
    if (!*fbo) gl.GenFramebuffers(1, fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

static int pass_size(const pass_t *p, int ax, int prev, int vp, int in_w)
{
    float s;
    switch (p->stype[ax]) {
    case ST_VIEWPORT: s = vp * p->scale[ax]; break;
    /* The encoder's 1280 samples are 4 a pixel of a console's 320-pixel line:
     * a widescreen line is longer, so it gets as many per pixel. The beam's
     * 8192 already has more than two samples per screen pixel at 4K, so it
     * stays as it is (scaling it too cost a quarter more GPU time for nothing). */
    case ST_ABSOLUTE: s = p->scale[ax] * (ax == 0 && in_w > 320 && strstr(p->shader, "encode") ? in_w / 320.0f : 1.0f); break;
    default:          s = prev * p->scale[ax]; break;
    }
    int n = (int)(s + 0.5f);
    return n < 1 ? 1 : n > max_tex ? max_tex : n;
}

/* a parameter over the preset's value (if the preset's shaders have it) */
static void set_param(const char *name, float v)
{
    int i = param_index(name, 0);
    if (i >= 0) P.pval[i] = v;
}

int crt_draw(const crt_look *look, const uint32_t *px, int pitch, int w, int h,
             int console, SDL_FRect dst, uint64_t frame)
{
    int preset = look->preset;
    if (!ok || preset < 1 || preset >= CRT_PRESET_COUNT) return 0;
    int vw = (int)SDL_floorf(dst.w + 0.5f), vh = (int)SDL_floorf(dst.h + 0.5f);
    if (vw < 1 || vh < 1) return 0;

    char key[128];
    SDL_snprintf(key, sizeof key, "lite/%s/%s.glslp", look->flat ? "flat" : "curved", preset_file[preset]);
    if (strcmp(key, P.key)) {
        SDL_FlushRenderer(R);
        SDL_strlcpy(P.key, key, sizeof P.key);
        uint64_t t0 = SDL_GetTicksNS();
        P.failed = !load_preset(key);
        if (P.failed) { free_preset(); SDL_snprintf(why, sizeof why, "The CRT preset %s could not be loaded (see the log).", key); }
        else memcpy(P.pdef, P.pval, sizeof P.pdef);
        if (!P.failed) SDL_Log("crt: %s, %d passes, %.0f ms", key, P.n, (SDL_GetTicksNS() - t0) / 1e6);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);   /* the window; SDL binds its own target again */
    }
    if (P.failed) return 0;

    /* the same game frame with the same options: the picture is already in
     * out_tex (menus, pause, a refresh shown twice): no GPU work */
    static struct { uint64_t frame; crt_look look; int w, h, vw, vh, console; const uint32_t *px; } last;
    if (out_tex && last.frame == frame && !memcmp(&last.look, look, sizeof *look) && last.w == w && last.h == h &&
        last.vw == vw && last.vh == vh && last.console == console && last.px == px) {
        SDL_FRect d = { SDL_floorf(dst.x + 0.5f), SDL_floorf(dst.y + 0.5f), (float)vw, (float)vh };
        SDL_RenderTexture(R, out_tex, NULL, &d);
        return 1;
    }
    last.frame = frame; last.look = *look; last.w = w; last.h = h; last.vw = vw; last.vh = vh;
    last.console = console; last.px = px;

    /* the output: an SDL target texture, drawn by SDL afterwards */
    if (!out_tex || out_w != vw || out_h != vh) {
        if (out_tex) SDL_DestroyTexture(out_tex);
        out_tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, vw, vh);
        if (!out_tex) return 0;
        SDL_SetTextureBlendMode(out_tex, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(out_tex, SDL_SCALEMODE_NEAREST);
        SDL_PropertiesID pr = SDL_GetTextureProperties(out_tex);
        out_gl = (GLuint)SDL_GetNumberProperty(pr, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
        out_target = (GLenum)SDL_GetNumberProperty(pr, SDL_PROP_TEXTURE_OPENGL_TEXTURE_TARGET_NUMBER, GL_TEXTURE_2D);
        out_w = vw; out_h = vh;
        if (out_fbo) { gl.DeleteFramebuffers(1, &out_fbo); out_fbo = 0; }
    }
    if (!out_gl) return 0;

    SDL_FlushRenderer(R);
    GLint old_fbo = 0, old_vp[4], old_vao = 0, old_buf = 0, old_row = 0, old_align = 4;
    gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    gl.GetIntegerv(GL_VIEWPORT, old_vp);
    gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
    gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buf);
    gl.GetIntegerv(GL_UNPACK_ROW_LENGTH, &old_row);
    gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &old_align);
    GLboolean blend = gl.IsEnabled(GL_BLEND), scissor = gl.IsEnabled(GL_SCISSOR_TEST);
    gl.Disable(GL_BLEND);
    gl.Disable(GL_SCISSOR_TEST);

    if (!vao) {
        static const GLfloat quad[] = {   /* VertexCoord (x, y, z, w), TexCoord (s, t) */
            0, 0, 0, 1, 0, 0,   1, 0, 0, 1, 1, 0,   0, 1, 0, 1, 0, 1,   1, 1, 0, 1, 1, 1,
        };
        gl.GenVertexArrays(1, &vao);
        gl.BindVertexArray(vao);
        gl.GenBuffers(1, &vbo);
        gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl.BufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)0);
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)(4 * sizeof(GLfloat)));
    }
    gl.BindVertexArray(vao);
    if (!out_fbo) {
        gl.GenFramebuffers(1, &out_fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, out_fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, out_target, out_gl, 0);
    }

    /* the frame: row 0 is the top line, as every pass and SDL expect */
    gl.ActiveTexture(GL_TEXTURE0);
    if (!in_tex || in_w != w || in_h != h) {
        if (!in_tex) gl.GenTextures(1, &in_tex);
        gl.BindTexture(GL_TEXTURE_2D, in_tex);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        in_w = w; in_h = h;
    }
    gl.BindTexture(GL_TEXTURE_2D, in_tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);

    /* the game's own settings over the preset's: the Mega Drive's console
     * timing (Auto would guess from the frame size, which widescreen changes),
     * and the 224 lines filling the picture height, so the picture covers
     * the same area as without the filter; then the player's options */
    memcpy(P.pval, P.pdef, sizeof P.pval);
    set_param("LR_CONSOLE", (float)console);
    set_param("LR_FILL", 1);
    if (!look->glow) { set_param("LR_GLOW", 0); set_param("LR_REFLECT", 0); }
    if (!look->wires) set_param("LR_DAMPER", 0);
    if (look->dither) set_param("LR_DBLEND_AUTO", 1);
    if (look->mask) {   /* below 4K a real mask is finer than the screen can show */
        int ms = param_index("LR_MASK_STR", 0);
        if (ms >= 0) P.pval[ms] = look->mask == 1 ? P.pdef[ms] * 0.5f : 0.0f;
    }
    if (look->antimoire) {
        set_param("LR_MASK_PX", 1);     /* auto: whole screen pixels nearest the preset's tube */
        set_param("LR_SMOOTH", 1);      /* each pixel shows the light averaged over its height */
    }

    /* collect finished timestamps (a set per frame: before the passes and
     * after each), start this frame's */
    if (!queries[0][0]) gl.GenQueries(NQ * (MAX_PASSES + 1), &queries[0][0]);
    for (int q = 0; q < NQ; q++) {
        int np = q_pending[q];
        GLint avail = 0;
        if (!np) continue;
        gl.GetQueryObjectiv(queries[q][np], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) continue;
        GLuint64 t[MAX_PASSES + 1];
        for (int i = 0; i <= np; i++) gl.GetQueryObjectui64v(queries[q][i], GL_QUERY_RESULT, &t[i]);
        q_pending[q] = 0;
        if (np != P.n) continue;                        /* from another preset */
        int first = !gpu_ms_t || SDL_GetTicksNS() - gpu_ms_t > 1000000000ull;
        for (int i = 0; i < np; i++) {
            float ms = (t[i + 1] - t[i]) / 1e6f;
            pass_ms[i] = first ? ms : pass_ms[i] * 0.9f + ms * 0.1f;
        }
        float ms = (t[np] - t[0]) / 1e6f;
        gpu_ms = first ? ms : gpu_ms * 0.9f + ms * 0.1f;
        gpu_ms_t = SDL_GetTicksNS();
    }
    int q_this = -1;
    if (!q_pending[q_next]) {
        q_this = q_next;
        q_next = (q_next + 1) % NQ;
        gl.QueryCounter(queries[q_this][0], GL_TIMESTAMP);
    }
    SDL_snprintf(look_desc, sizeof look_desc, "%s %s, %dx%d", crt_names[preset], look->flat ? "flat" : "curved", vw, vh);

    static const GLfloat mvp[16] = { 2, 0, 0, 0,  0, 2, 0, 0,  0, 0, -1, 0,  -1, -1, 0, 1 };
    int prev_w = w, prev_h = h;
    GLuint prev_tex = in_tex;
    for (int i = 0; i < P.n; i++) {
        pass_t *p = &P.p[i];
        int last = i == P.n - 1;
        int pw = last ? vw : pass_size(p, 0, prev_w, vw, w);
        int ph = last ? vh : pass_size(p, 1, prev_h, vh, w);
        if (!last && (!p->tex || p->w != pw || p->h != ph)) {
            make_target(&p->tex, &p->fbo, pw, ph, p->fp);
            p->w = pw; p->h = ph;
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, last ? out_fbo : p->fbo);
        gl.Viewport(0, 0, pw, ph);
        gl.UseProgram(p->prog);

        int unit = 2;
        for (int k = 0; k < i; k++) {
            if (p->u_alias[k] < 0) continue;
            gl.ActiveTexture(GL_TEXTURE0 + unit);
            sampler(P.p[k].tex, P.p[k + 1].linear, P.p[k + 1].wrap);
            gl.Uniform1i(p->u_alias[k], unit++);
        }
        gl.ActiveTexture(GL_TEXTURE1);
        sampler(in_tex, P.p[0].linear, P.p[0].wrap);
        gl.ActiveTexture(GL_TEXTURE0);
        sampler(prev_tex, p->linear, p->wrap);
        if (p->u_tex >= 0) gl.Uniform1i(p->u_tex, 0);
        if (p->u_orig >= 0) gl.Uniform1i(p->u_orig, 1);
        if (p->u_texsize >= 0) gl.Uniform2f(p->u_texsize, (float)prev_w, (float)prev_h);
        if (p->u_insize >= 0) gl.Uniform2f(p->u_insize, (float)prev_w, (float)prev_h);
        if (p->u_outsize >= 0) gl.Uniform2f(p->u_outsize, (float)pw, (float)ph);
        if (p->u_origsize >= 0) gl.Uniform2f(p->u_origsize, (float)w, (float)h);
        if (p->u_originsize >= 0) gl.Uniform2f(p->u_originsize, (float)w, (float)h);
        if (p->u_frame >= 0) gl.Uniform1i(p->u_frame, (GLint)(frame & 0x7FFFFFFF));
        if (p->u_framedir >= 0) gl.Uniform1i(p->u_framedir, 1);
        if (p->u_mvp >= 0) gl.UniformMatrix4fv(p->u_mvp, 1, GL_FALSE, mvp);
        for (int k = 0; k < P.np; k++) if (p->u_param[k] >= 0) gl.Uniform1f(p->u_param[k], P.pval[k]);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (q_this >= 0) gl.QueryCounter(queries[q_this][i + 1], GL_TIMESTAMP);
        prev_tex = p->tex;
        prev_w = pw; prev_h = ph;
    }
    if (q_this >= 0) q_pending[q_this] = P.n;
    /* At most one filtered frame in flight: when the GPU is slower than the
     * game, frames would otherwise pile up (seen headless, where nothing
     * else waits for the GPU, as a blank window), and the picture lags. */
    if (fence) {
        gl.ClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        gl.DeleteSync(fence);
    }
    fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    /* put back what SDL doesn't set again by itself */
    for (int u = 7; u >= 0; u--) { gl.ActiveTexture(GL_TEXTURE0 + u); gl.BindTexture(GL_TEXTURE_2D, 0); }
    gl.UseProgram(0);
    gl.BindVertexArray((GLuint)old_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buf);
    gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)old_fbo);
    gl.Viewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3]);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, old_row);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, old_align);
    if (blend) gl.Enable(GL_BLEND);
    if (scissor) gl.Enable(GL_SCISSOR_TEST);

    SDL_FRect d = { SDL_floorf(dst.x + 0.5f), SDL_floorf(dst.y + 0.5f), (float)vw, (float)vh };
    SDL_RenderTexture(R, out_tex, NULL, &d);
    return 1;
}
