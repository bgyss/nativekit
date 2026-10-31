#!/bin/sh
# Fetch and build the tools the tests need, at pinned commits, inside build/.
#
#   sh tools/toolchains/setup.sh          asar and the reference core
#   sh tools/toolchains/setup.sh asar     one component (asar | bsnes | all)
#
# Sources land in build/toolchains/src/<name>; asar is built to
# build/toolchains/asar/asar and the bsnes libretro core (the reference core,
# bsnes at its pin plus stuntracefx's sfxbox patches plus our 0100 patch) to
# build/oracle-core/bsnes_libretro.{dylib,so,dll}. Nothing is installed on the host.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
tc="$top/build/toolchains"
. "$here/pins.env"
mkdir -p "$tc/src"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

case "$(uname -s)" in
    Darwin) ext=dylib ;;
    MINGW*|MSYS*|CYGWIN*) ext=dll ;;
    *) ext=so ;;
esac

# fetch name url pin
fetch() {
    d="$tc/src/$1"
    if [ ! -d "$d/.git" ]; then
        git init -q "$d"
        git -C "$d" remote add origin "$2"
    fi
    if [ "$(git -C "$d" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        git -C "$d" fetch -q --depth 1 origin "$3"
        git -C "$d" checkout -q --force "$3"
    fi
}

# Patches: stuntracefx's series, then ours, applied on a clean checkout and
# again whenever the set (or the stuntracefx pin) changes.
patch_bsnes() {
    fetch stuntracefx "$STUNTRACEFX_URL" "$STUNTRACEFX_PIN"
    d="$tc/src/bsnes"
    sfx="$tc/src/stuntracefx/tools/toolchains/patches/bsnes"
    ours="$here/patches/bsnes"
    stamp="$( (cat "$sfx"/*.patch "$ours"/*.patch; echo "$BSNES_PIN") | shasum | cut -c1-40)"
    if [ "$(cat "$d/.patch-stamp" 2>/dev/null || true)" != "$stamp" ]; then
        git -C "$d" reset -q --hard "$BSNES_PIN"
        git -C "$d" clean -qfdx
        for p in "$sfx"/*.patch "$ours"/*.patch; do git -C "$d" apply "$p"; done
        echo "$stamp" > "$d/.patch-stamp"
    fi
}

build_asar() {
    fetch asar "$ASAR_URL" "$ASAR_PIN"
    cmake -S "$tc/src/asar/src" -B "$tc/asar-build" -DCMAKE_BUILD_TYPE=Release \
        -DASAR_GEN_LIB=OFF -DASAR_GEN_DLL=OFF \
        -DASAR_GEN_EXE_TEST=OFF -DASAR_GEN_DLL_TEST=OFF >/dev/null 2>&1
    cmake --build "$tc/asar-build" -j "$jobs" >"$tc/asar-build.log" 2>&1
    mkdir -p "$tc/asar"
    cp "$(find "$tc/asar-build" -type f \( -name asar -o -name asar.exe \) -perm -u+x | head -1)" "$tc/asar/asar"
    "$tc/asar/asar" --version | head -1
}

build_bsnes() {
    fetch bsnes "$BSNES_URL" "$BSNES_PIN"
    patch_bsnes
    make -C "$tc/src/bsnes/bsnes" target=libretro binary=library -j "$jobs" >"$tc/bsnes-build.log" 2>&1
    mkdir -p "$top/build/oracle-core"
    cp "$tc/src/bsnes/bsnes/out/bsnes_libretro.$ext" "$top/build/oracle-core/"
    echo "reference core: $top/build/oracle-core/bsnes_libretro.$ext"
    echo "sfxbox symbols exported: $(nm -g "$top/build/oracle-core/bsnes_libretro.$ext" | grep sfxbox_ | grep -vc ' U ' || true)"
}

what="${1:-all}"
case "$what" in
    asar) build_asar ;;
    bsnes) build_bsnes ;;
    all) build_asar; build_bsnes ;;
    *) echo "unknown component: $what" >&2; exit 2 ;;
esac
