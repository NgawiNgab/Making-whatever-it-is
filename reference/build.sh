#!/usr/bin/env bash
# Android mod build for Minecraft Bedrock 1.26.62.1 / arm64-v8a.
#
# This is device-only. It has NOT been run against a real NDK in this workspace (no
# NDK and no device are available), so its correctness is limited to: it configures
# and fails loudly when something is missing. CMake does the work; the old version of
# this script compiled `find src` by hand from wrong paths, skipped src/baritone
# entirely, and omitted main.cpp / the JNI entry point, so a "successful" build would
# have produced a library nothing could load.
#
# Requirements, checked below:
#   * an Android NDK (ANDROID_NDK_HOME or --ndk), API 21+, arm64-v8a
#   * ./scripts/bootstrap.sh has run (fmt + EnTT + CMake)
#
# Usage:
#   ./reference/build.sh                 # configure + build + package
#   ./reference/build.sh --configure-only
#   ANDROID_NDK_HOME=/path/to/ndk ./reference/build.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-android}"
MODE="build"

while [ $# -gt 0 ]; do
    case "$1" in
        --configure-only) MODE="configure" ;;
        --build-dir) shift; BUILD="$1" ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "build.sh: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

fail() { echo "android-build: $*" >&2; exit 1; }

[ -f "$ROOT/third_party/env.sh" ] || fail "run ./scripts/bootstrap.sh first (no third_party/env.sh)"
# shellcheck disable=SC1091
source "$ROOT/third_party/env.sh"

[ -n "${ANDROID_NDK_HOME:-}" ] || fail "ANDROID_NDK_HOME is not set; this build needs an Android NDK"
[ -d "$ANDROID_NDK_HOME" ] || fail "ANDROID_NDK_HOME does not exist: $ANDROID_NDK_HOME"
TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
[ -f "$TOOLCHAIN" ] || fail "no CMake toolchain at $TOOLCHAIN (is this an NDK?)"
[ -x "$CMAKE" ] || fail "CMake missing: run ./scripts/bootstrap.sh"

echo "android-build: ndk=$ANDROID_NDK_HOME"
echo "android-build: cmake=$("$CMAKE" --version | head -1)"

"$CMAKE" -S "$ROOT" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$BUILD/bin"

if [ "$MODE" = "configure" ]; then
    echo "ANDROID_CONFIGURE_OK -> $BUILD"
    exit 0
fi

"$CMAKE" --build "$BUILD" --target baritone_package -j "$(nproc)"

PACKAGE="$(find "$BUILD/reference/baritone-android" -name '*.levipack' -print -quit 2>/dev/null || true)"
[ -n "$PACKAGE" ] && [ -f "$PACKAGE" ] || fail "build finished but no .levipack was produced"
echo "ANDROID_BUILD_OK -> $PACKAGE"
echo "android-build: NOT device-verified; see docs/features/AUTO_MINE.md for the open blockers"
