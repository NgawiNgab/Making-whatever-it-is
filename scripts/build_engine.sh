#!/usr/bin/env bash
# Builds the first-party headless engine objects into the layout tests/run.sh expects:
#
#   build/obj/reference/*.o   the vendored core + the few platform helpers it needs
#   build/obj/sim/*.o         src/sim/*
#
# scripts/build.sh (CMake) is the normal path for the CLI. This script exists so the
# regression harness can be built with exactly the same flags as every other harness
# in tests/run.sh, without requiring a CMake configure step in the test loop.
#
# Everything here is first-party or vendored-here source; no game process is involved.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/reference/baritone-android/src"
BUILD="$ROOT/build"
REFOBJ="$BUILD/obj/reference"
SIMOBJ="$BUILD/obj/sim"
MARKER="$BUILD/.engine-fingerprint"

if [ ! -f "$ROOT/third_party/env.sh" ]; then
    "$ROOT/scripts/bootstrap.sh"
fi
# shellcheck disable=SC1091
source "$ROOT/third_party/env.sh"

if [ -n "${ZIG:-}" ] && [ -x "$ZIG" ]; then CXX=("$ZIG" c++)
elif command -v clang++ >/dev/null 2>&1; then CXX=("clang++")
else CXX=("g++"); fi

mkdir -p "$REFOBJ" "$SIMOBJ"

FLAGS=(-std=c++23 -O1 -g0 -I"$SRC" -I"$ROOT/src" -I"$ROOT")

# Rebuild everything when an input that changes object layout changed. Cheap: a
# cksum over headers/sources plus compiler identity.
FINGERPRINT="$(
    {
        find "$SRC/baritone" "$ROOT/src" -type f \( -name '*.h' -o -name '*.cpp' \) -print0 |
            sort -z | xargs -0 cat | cksum
        cksum "$SRC/platform/BlockClassifier.cpp" "$SRC/platform/BlockClassifier.h" \
              "$SRC/sdk/Nbt.cpp" "$SRC/sdk/Nbt.h" 2>/dev/null
        "${CXX[@]}" --version 2>/dev/null | head -2 | cksum
        printf '%s\n' "${CXX[*]}" "${FLAGS[*]}"
    } | cksum
)"
if [ "${FORCE_REBUILD:-0}" = "1" ] || [ ! -f "$MARKER" ] || [ "$(cat "$MARKER")" != "$FINGERPRINT" ]; then
    rm -f "$REFOBJ"/*.o "$SIMOBJ"/*.o
    printf '%s\n' "$FINGERPRINT" > "$MARKER"
fi

compile() {  # compile <src> <obj> ; echoes the failing source on error
    if ! "${CXX[@]}" "${FLAGS[@]}" -c "$1" -o "$2" 2>"$2.log"; then
        echo "compile failed: $1" >&2
        head -20 "$2.log" >&2
        return 1
    fi
}

export -f compile
export CXX_STR="${CXX[*]}"

# Core: needed by every harness and by the simulation engine.
find "$SRC/baritone" -name '*.cpp' | sort | while read -r f; do
    n="$(echo "${f#"$SRC"/}" | sed 's|/|_|g; s|\.cpp$|.o|')"
    printf '%s\0%s\0' "$f" "$REFOBJ/$n"
done | xargs -0 -P "$(nproc)" -n 2 sh -c '
    if ! '"${CXX[*]}"' -std=c++23 -O1 -g0 -I"'"$SRC"'" -I"'"$ROOT/src"'" -I"'"$ROOT"'" -c "$0" -o "$1" 2>"$1.log"; then
        echo "FAILED $0" >&2
        head -10 "$1.log" >&2
        exit 1
    fi
'
for extra in "$SRC/platform/BlockClassifier.cpp" "$SRC/sdk/Nbt.cpp"; do
    n="$(echo "${extra#"$SRC"/}" | sed 's|/|_|g; s|\.cpp$|.o|')"
    compile "$extra" "$REFOBJ/$n"
done

# First-party simulation engine (no main()).
for f in "$ROOT"/src/sim/*.cpp; do
    [ -e "$f" ] || continue
    n="$(echo "${f#"$ROOT"/}" | sed 's|/|_|g; s|\.cpp$|.o|')"
    compile "$f" "$SIMOBJ/$n"
done

# The CLI is the same engine plus one entry point; linking it here keeps the test
# runner independent of a CMake configure step. scripts/build.sh builds it with CMake.
mkdir -p "$BUILD/bin"
compile "$ROOT/src/cli/main.cpp" "$SIMOBJ/cli_main.cpp.o"
"${CXX[@]}" -o "$BUILD/bin/baritone-sim" "$SIMOBJ"/*.o "$REFOBJ"/*.o -lpthread

echo "ENGINE_OBJECTS_OK reference=$(find "$REFOBJ" -name '*.o' | wc -l) sim=$(find "$SIMOBJ" -name '*.o' | wc -l) cli=$BUILD/bin/baritone-sim"
