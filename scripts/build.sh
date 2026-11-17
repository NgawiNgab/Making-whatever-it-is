#!/usr/bin/env bash
# Configure and build the verified host engine/CLI with the pinned C++23 toolchain.
#
#   ./scripts/build.sh          # incremental CMake build
#   ./scripts/build.sh --clean  # remove build/ and configure from scratch
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
if [ ! -f "$ROOT/third_party/env.sh" ]; then
    "$ROOT/scripts/bootstrap.sh"
fi
# shellcheck disable=SC1091
source "$ROOT/third_party/env.sh"

for arg in "$@"; do
    case "$arg" in
        --clean)
            rm -rf "$BUILD"
            ;;
        -h|--help)
            sed -n '2,6p' "$0"
            exit 0
            ;;
        *)
            echo "build: unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

mkdir -p "$BUILD" "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"
"$CMAKE" -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_COMPILER="$ROOT/scripts/zig-cxx" \
    -DENTT_DIR="$ROOT/third_party/entt" \
    -DFMT_DIR="$ROOT/third_party/fmt" \
    -DPRELOADER_ANDROID_DIR="$ROOT/reference/preloader-android"
"$CMAKE" --build "$BUILD" --parallel "$(nproc)"
[ -x "$BUILD/bin/baritone-sim" ] || { echo "build: baritone-sim missing" >&2; exit 1; }
echo "BUILD_OK -> $BUILD/bin/baritone-sim"
