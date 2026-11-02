#!/bin/sh
# Fetch and build the tools the tests need, at pinned commits, inside build/.
#
#   sh tools/toolchains/setup.sh          asar and the reference core
#   sh tools/toolchains/setup.sh asar     one component (asar | bsnes | blastem | vasm | all)
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

# The new tools are never reset or cleaned over local work. Tracked edits stop
# builds; untracked files also prevent changing the checked-out revision.
fetch_safe() {
    d="$tc/src/$1"
    if [ ! -d "$d/.git" ]; then
        git init -q "$d"
        git -C "$d" remote add origin "$2"
    fi
    if [ -n "$(git -C "$d" status --porcelain --untracked-files=no)" ]; then
        echo "refusing to build a checkout with tracked edits: $1" >&2
        exit 1
    fi
    if [ "$(git -C "$d" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        if [ -n "$(git -C "$d" status --porcelain --untracked-files=normal)" ]; then
            echo "refusing to change a checkout with local work: $1" >&2
            exit 1
        fi
        git -C "$d" fetch -q --depth 1 origin "$3"
        git -C "$d" checkout -q --detach "$3"
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

# logged log cmd...: run cmd with its output in log; on failure, the log's tail
logged() {
    log="$1"; shift
    if ! "$@" >"$log" 2>&1; then
        echo "failed: $*" >&2
        echo "the end of $log:" >&2
        tail -n 30 "$log" >&2
        exit 1
    fi
}

build_asar() {
    fetch asar "$ASAR_URL" "$ASAR_PIN"
    logged "$tc/asar-configure.log" cmake -S "$tc/src/asar/src" -B "$tc/asar-build" -DCMAKE_BUILD_TYPE=Release \
        -DASAR_GEN_LIB=OFF -DASAR_GEN_DLL=OFF \
        -DASAR_GEN_EXE_TEST=OFF -DASAR_GEN_DLL_TEST=OFF
    logged "$tc/asar-build.log" cmake --build "$tc/asar-build" -j "$jobs"
    mkdir -p "$tc/asar"
    cp "$(find "$tc/asar-build" -type f \( -name asar -o -name asar.exe \) -perm -u+x | head -1)" "$tc/asar/asar"
    "$tc/asar/asar" --version | head -1
}

build_bsnes() {
    fetch bsnes "$BSNES_URL" "$BSNES_PIN"
    patch_bsnes
    logged "$tc/bsnes-build.log" make -C "$tc/src/bsnes/bsnes" target=libretro binary=library -j "$jobs"
    mkdir -p "$top/build/oracle-core"
    cp "$tc/src/bsnes/bsnes/out/bsnes_libretro.$ext" "$top/build/oracle-core/"
    echo "reference core: $top/build/oracle-core/bsnes_libretro.$ext"
    n="$(nm -g "$top/build/oracle-core/bsnes_libretro.$ext" | grep sfxbox_ | grep -vc ' U ' || true)"
    echo "sfxbox symbols exported: $n"
    if [ "$n" -eq 0 ]; then
        echo "the reference core exports no sfxbox_ symbols: the patches did not take" >&2
        exit 1
    fi
}

build_vasm() {
    fetch_safe vasm "$VASM_URL" "$VASM_PIN"
    d="$tc/src/vasm"
    logged "$tc/vasm-m68k.log" make -C "$d" CPU=m68k SYNTAX=mot -j "$jobs"
    logged "$tc/vasm-z80.log" make -C "$d" CPU=z80 SYNTAX=oldstyle -j "$jobs"
    mkdir -p "$tc/vasm"
    cp "$d/vasmm68k_mot" "$d/vasmz80_oldstyle" "$tc/vasm/"
    python3 "$top/consoles/md/tests/toolchain_check.py"
}

build_blastem() {
    fetch_safe blastem-source "$BLASTEM_URL" "$BLASTEM_PIN"
    d="$tc/src/blastem-source"
    patchdir="$here/patches/blastem"
    stamp="$( (cat "$patchdir"/*.patch "$top/consoles/md/tests/oracle/trace_abi.c" \
        "$top/consoles/md/tests/oracle/trace_abi.h"; echo "$BLASTEM_PIN") | shasum | cut -c1-40)"
    buildsrc="$tc/src/blastem-build-$stamp"
    if [ ! -d "$buildsrc" ]; then
        mkdir "$buildsrc"
        git -C "$d" archive "$BLASTEM_PIN" | tar -x -C "$buildsrc"
        for p in "$patchdir"/*.patch; do patch -d "$buildsrc" -p1 < "$p"; done
        cp "$top/consoles/md/tests/oracle/trace_abi.c" "$top/consoles/md/tests/oracle/trace_abi.h" "$buildsrc/"
        echo "$stamp" > "$buildsrc/.nativekit-patch-stamp"
    elif [ "$(cat "$buildsrc/.nativekit-patch-stamp" 2>/dev/null || true)" != "$stamp" ]; then
        echo "refusing to reuse a BlastEm build tree with a mismatched patch stamp" >&2
        exit 1
    fi
    source_stamp() {
        (cd "$buildsrc" && shasum Makefile genesis.c link.T m68k.cpu m68k_util.c trace_abi.c trace_abi.h) |
            shasum | cut -d ' ' -f 1
    }
    current_source_stamp="$(source_stamp)"
    recorded_source_stamp="$(cat "$buildsrc/.nativekit-source-stamp" 2>/dev/null || true)"
    if [ -n "$recorded_source_stamp" ] && [ "$current_source_stamp" != "$recorded_source_stamp" ]; then
        echo "refusing to build a BlastEm tree with local source edits" >&2
        exit 1
    fi
    if [ -z "$recorded_source_stamp" ]; then
        echo "$current_source_stamp" > "$buildsrc/.nativekit-source-stamp"
    fi
    logged "$tc/blastem-build.log" make -C "$buildsrc" "libblastem.$ext" NOLTO=1 -j "$jobs"
    mkdir -p "$tc/blastem"
    cp "$buildsrc/libblastem.$ext" "$tc/blastem/"
    echo "candidate reference core: $tc/blastem/libblastem.$ext"
}

what="${1:-all}"
case "$what" in
    asar) build_asar ;;
    bsnes) build_bsnes ;;
    vasm) build_vasm ;;
    blastem) build_blastem ;;
    all) build_asar; build_bsnes ;;
    *) echo "unknown component: $what" >&2; exit 2 ;;
esac
