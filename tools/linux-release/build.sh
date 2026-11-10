#!/bin/sh
# Builds the Linux release archive: one program that needs only glibc 2.35+
# and the graphics drivers (SDL3 built in).
# Usage (from the repository root, after tools/trace.py and tools/recomp.py
# for each game you have): tools/linux-release/build.sh <version> [out-dir]
# Needs gcc/g++, cmake, curl, dpkg-deb, and the X11/Wayland/ALSA/PulseAudio/
# PipeWire development headers (SDL loads those libraries at run time).
set -eu
VER=${1:?version, e.g. v0.8.2-linux1}
OUT=$(realpath -m "${2:-build-linux-release}")
SRC=$(pwd)
HERE=$SRC/tools/linux-release
SR=$OUT/sysroot
JL=$SR/usr/lib/x86_64-linux-gnu
mkdir -p "$OUT"

# 1. Sysroot with Ubuntu 22.04's C library.
if [ ! -f "$JL/libc.so" ]; then
    mkdir -p "$OUT/debs"
    MIRROR=http://archive.ubuntu.com/ubuntu
    curl -fsS "$MIRROR/dists/jammy-updates/main/binary-amd64/Packages.gz" | gunzip > "$OUT/debs/Packages"
    for p in libc6 libc6-dev linux-libc-dev; do
        f=$(awk -v p="$p" '$1=="Package:"{n=$2} $1=="Filename:" && n==p {print $2}' "$OUT/debs/Packages" | tail -1)
        curl -fsS -o "$OUT/debs/$p.deb" "$MIRROR/$f"
        dpkg-deb -x "$OUT/debs/$p.deb" "$SR"
    done
    # Absolute links would point into the build machine: make them relative.
    find "$SR" -type l -lname '/*' | while read -r l; do
        ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$SR$(readlink "$l")")" "$l"
    done
fi

# 2. Two functions GCC's static libstdc++ wants that glibc 2.35 lacks.
gcc --sysroot="$SR" -O2 -c "$HERE/glibc-compat.c" -o "$OUT/glibc-compat.o"

# 3. Build. GCC searches the build machine's libraries before the sysroot's,
# so the sysroot's come first. Two SDL checks link against the build
# machine's X11 and fail with the older glibc; their answer is yes.
cmake -S "$SRC" -B "$OUT/build" -DCMAKE_TOOLCHAIN_FILE="$HERE/jammy-toolchain.cmake" \
    -DDS_SYSROOT="$SR" -DCMAKE_BUILD_TYPE=Release -DDS_SYSTEM_SDL=OFF -DDS_BUILD_TESTS=OFF \
    "-DCMAKE_EXE_LINKER_FLAGS=-B$JL -L$JL -L$SR/lib/x86_64-linux-gnu -Wl,-rpath-link=$SR/lib/x86_64-linux-gnu:$JL -static-libstdc++ -static-libgcc $OUT/glibc-compat.o" \
    -DHAVE_XGENERICEVENT=1 -DSDL_VIDEO_DRIVER_X11_HAS_XKBLOOKUPKEYSYM=1
cmake --build "$OUT/build" --parallel --target StrikeCollectionHD
BIN=$OUT/build/StrikeCollectionHD
NEED=$(objdump -T "$BIN" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)
[ "$NEED" = GLIBC_2.35 ] || { echo "needs $NEED, expected GLIBC_2.35" >&2; exit 1; }

# 4. Archive: program, launcher, readme, licenses.
GAMES=$(ls "$SRC/generated" | tr '\n' '-')
N=StrikeCollectionHD-$VER-${GAMES}x86_64
D=$OUT/$N
rm -rf "$D"; mkdir -p "$D/licenses" "$D/roms" "$D/batocera"
cp "$BIN" "$D/"; strip "$D/StrikeCollectionHD"
cp "$HERE/README.txt" "$D/"
cp "$HERE/StrikeHD.sh" "$D/batocera/"
cp "$SRC/LICENSE" "$D/LICENSE.txt"
cp "$OUT/build/_deps/sdl3-src/LICENSE.txt" "$D/licenses/SDL3.txt"
cp "$SRC/third_party/ymfm/LICENSE" "$D/licenses/ymfm.txt"
cp "$SRC/third_party/z80/LICENSE" "$D/licenses/z80.txt"
cp "$SRC/third_party/lola-retro/LICENSE" "$D/licenses/lola-retro.txt"
cp "$SRC/res/fonts/OFL.txt" "$D/licenses/ChakraPetch-OFL.txt"
cp "$SRC/harness/musashi/softfloat/README.txt" "$D/licenses/SoftFloat.txt"
sed -n 5,27p "$SRC/harness/musashi/m68kcpu.c" | sed 's/^ \* \{0,1\}//; s/^ \*\/$//' > "$D/licenses/Musashi.txt"
sed -n '/^This software is available under 2 licenses/,$p' "$SRC/third_party/stb/stb_image.h" | sed '/^\*\//,$d' > "$D/licenses/stb.txt"
tar -C "$OUT" -czf "$OUT/$N.tar.gz" "$N"
echo "$OUT/$N.tar.gz"
