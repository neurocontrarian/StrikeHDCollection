# Lola Retro (GLSL)

The Kodi GLSL port of [Lola Retro](https://github.com/zoran123456/lola-retro) (MIT, see `LICENSE`),
copied unchanged from `kodi/lola-retro/` at commit `e004d73` (2026-10-28). The files are generated
upstream by `tools/make_glsl.py`; to update, copy that folder again.

The game embeds these files at build time (`cmake/embed_files.cmake`) and runs the presets in
`src/host/crt.c` (Settings > Picture > CRT Filter).
