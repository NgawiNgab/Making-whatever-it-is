#!/usr/bin/env bash
# Builds and runs the headless tests.
#
# Two kinds of test live here:
#   * the baritone core - plain C++23, no game, no Android. Proves the mining loop.
#   * the mod layer      - needs stubs for jni.h / android/log.h / preloader exports.
#                          Proves the #mine command parser the mod menu calls.
#
# Neither needs a phone or a device, which is the point: the logic is checked before
# it is packaged. Missing dependencies are restored by scripts/bootstrap.sh.
#
#   ./tests/run.sh           # build + run everything
#   ./tests/run.sh --clean   # throw the object cache away first
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/reference/baritone-android/src"
OUT="${TEST_OUT_DIR:-${TMPDIR:-/tmp}/baritone-test}"
OBJ="$OUT/obj"
MODOBJ="$OUT/modobj"

if [ ! -f "$ROOT/third_party/env.sh" ] || \
   [ ! -f "$ROOT/third_party/entt/src/entt/entt.hpp" ] || \
   [ ! -f "$ROOT/third_party/fmt/include/fmt/format.h" ]; then
    echo "== bootstrapping pinned dependencies =="
    "$ROOT/scripts/bootstrap.sh" || exit 1
fi
# shellcheck disable=SC1091
source "$ROOT/third_party/env.sh"

if [ -n "${ZIG:-}" ] && [ -x "$ZIG" ]; then CXX=("$ZIG" c++)
elif command -v zig >/dev/null 2>&1; then CXX=("zig" c++)
elif command -v clang++ >/dev/null 2>&1; then CXX=("clang++" -stdlib=libc++)
else CXX=("g++"); fi

if [ "${1:-}" = "--clean" ]; then rm -rf "$OUT"; fi
mkdir -p "$OBJ" "$MODOBJ" "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"

# g++ 12 (the sandbox default) has no <format>; a clear error beats 83 opaque
# compile failures. The bootstrap installs Zig's clang/libc++ with C++23 support.
cat > "$OUT/compiler_probe.cpp" <<'EOF'
#include <format>
#include <string>
int main() { return std::format("{}", 42) == "42" ? 0 : 1; }
EOF
if ! "${CXX[@]}" -std=c++23 -O0 "$OUT/compiler_probe.cpp" -o "$OUT/compiler_probe" \
        > "$OUT/compiler_probe.log" 2>&1; then
    echo "C++23 toolchain lacks std::format; run ./scripts/bootstrap.sh" >&2
    head -8 "$OUT/compiler_probe.log" >&2
    exit 1
fi
"$OUT/compiler_probe" || { echo "C++23 compiler probe failed" >&2; exit 1; }

# ---------------------------------------------------------------- core objects
echo "== compiling core =="
find "$SRC/baritone" -name '*.cpp' | sort > "$OUT/srcs.txt"
MODFLAGS=(-std=c++23 -O1 -Wno-date-time -Wno-unused-result -DFMT_HEADER_ONLY
          -I"$SRC" -I"$ROOT" -I"$ROOT/tests/stubs"
          -I"$ROOT/third_party/entt/src" -I"$ROOT/third_party/fmt/include"
          -I"$ROOT/reference/preloader-android/include")

# Profiling showed a cold full build at 269 s and a warm one at 9.5 s. Object reuse
# is now the default, but a stale object with an old class layout once linked cleanly
# and then crashed, so every input that can affect object layout is fingerprinted:
# all sources/headers, external headers, compiler identity, flags, and this script.
FINGERPRINT="$(find \
        "$SRC" \
        "$ROOT/reference/preloader-android/include" \
        "$ROOT/tests/stubs" \
        "$ROOT/third_party/entt/src" \
        "$ROOT/third_party/fmt/include" \
        -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print0 \
    | sort -z | xargs -0 cat | cksum; \
    "${CXX[@]}" --version 2>/dev/null | head -2 | cksum; \
    printf '%s\n' "${CXX[*]}" "-std=c++23 -O1" "${MODFLAGS[*]:-}" | cksum; \
    cksum "$ROOT/tests/run.sh")"
if [ -f "$OUT/headers.fingerprint" ] && \
   [ "$(cat "$OUT/headers.fingerprint")" != "$FINGERPRINT" ]; then
    echo "  inputs changed, rebuilding"
    rm -f "$OBJ"/*.o
fi
if [ "${INCREMENTAL:-1}" != "1" ]; then
    rm -f "$OBJ"/*.o
else
    echo "  (incremental: reusing objects in $OBJ)"
fi
echo "$FINGERPRINT" > "$OUT/headers.fingerprint"

cat "$OUT/srcs.txt" | xargs -P "$(nproc)" -I{} sh -c '
    n=$(echo "{}" | sed "s|.*/baritone/||; s|/|_|g; s|\.cpp$|.o|")
    if [ ! -f "'"$OBJ"'/$n" ] || [ "{}" -nt "'"$OBJ"'/$n" ]; then
        '"${CXX[*]}"' -std=c++23 -O1 -I"'"$SRC"'" -c "{}" -o "'"$OBJ"'/$n" 2>"'"$OBJ"'/$n.log" || echo "{}"
    fi
' > "$OUT/core_failures.txt"
if [ -s "$OUT/core_failures.txt" ]; then
    echo "core compile failed:"; cat "$OUT/core_failures.txt"; exit 1
fi
echo "core: $(wc -l < "$OUT/srcs.txt") files"

# ------------------------------------------------------------ mod-layer objects
# Only the files a test needs are built; the hooks and JNI entry points cannot be
# linked on a desktop and are not what these tests cover.
echo "== compiling mod layer =="
MODS=(platform/BaritoneRuntime.cpp platform/GamePlayerContext.cpp platform/InputOverride.cpp
      platform/Log.cpp platform/Paths.cpp platform/PathVisualizer.cpp
      platform/BlockClassifier.cpp platform/WorldTracker.cpp platform/WorldTrackerScan.cpp
      platform/BlockBreaker.cpp
      sdk/Game.cpp sdk/Memory.cpp sdk/Nbt.cpp)
modfail=0
for m in "${MODS[@]}"; do
    n=$(echo "$m" | sed 's|/|_|g; s|\.cpp$|.o|')
    if ! "${CXX[@]}" "${MODFLAGS[@]}" -c "$SRC/$m" -o "$MODOBJ/$n" 2>"$MODOBJ/$n.log"; then
        echo "compile failed: $m"; head -8 "$MODOBJ/$n.log"; modfail=1
    fi
done
[ $modfail -ne 0 ] && exit 1
echo "mod layer: ${#MODS[@]} files"

# ----------------------------------------------------------------------- tests
status=0
passed=0
failed=0
for test in "$ROOT"/tests/*_harness.cpp; do
    name=$(basename "$test" .cpp)
    case "$name" in
        mine_command_harness|breaker_harness)
            # needs the mod layer + preloader stubs
            extra=("$MODOBJ"/*.o "$OBJ"/*.o "$ROOT/tests/stubs/preloader_lite.cpp"
                   "$ROOT/tests/stubs/ubsan_lite.cpp")
            flags=("${MODFLAGS[@]}")
            ;;
        sim_harness)
            # first-party simulation engine; see scripts/build.sh for the same objects
            extra=("$ROOT/build/obj/sim"/*.o "$ROOT/build/obj/reference"/*.o "$OBJ"/*.o)
            flags=(-std=c++23 -O1 -I"$ROOT/src" -I"$SRC" -I"$ROOT")
            ;;
        *)
            # core only
            extra=("$OBJ"/*.o "$SRC/platform/BlockClassifier.cpp" "$SRC/sdk/Nbt.cpp")
            flags=(-std=c++23 -O1 -I"$SRC" -I"$ROOT")
            ;;
    esac
    echo
    echo "== $name =="
    if ! "${CXX[@]}" "${flags[@]}" "$test" "${extra[@]}" -o "$OUT/$name" -lpthread \
            > "$OUT/$name.build.log" 2>&1; then
        echo "build failed:"; head -12 "$OUT/$name.build.log"; status=1; failed=$((failed+1)); continue
    fi
    if "$OUT/$name"; then passed=$((passed+1)); else status=1; failed=$((failed+1)); fi
done

echo
echo "== python regression tests =="
if (cd "$ROOT" && python3 -m unittest discover -s tests -p 'test_*.py' -v); then
    python_status=0
else
    python_status=1
    status=1
fi

echo
echo "C++ harnesses: $passed passed, $failed failed; Python suites: $([ "$python_status" -eq 0 ] && echo passed || echo failed)"
if [ $status -eq 0 ]; then echo "TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $status
