#!/usr/bin/env bash
# Builds and runs the headless tests.
#
# Two kinds of test live here:
#   * the baritone core - plain C++23, no game, no Android. Proves the mining loop.
#   * the mod layer      - needs stubs for jni.h / android/log.h / preloader exports.
#                          Proves the #mine command parser the mod menu calls.
#
# Neither needs a phone or a device, which is the point: the logic is checked before
# it is packaged.
#
#   ./tests/run.sh           # build + run everything
#   ./tests/run.sh --clean   # throw the object cache away first
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/reference/baritone-android/src"
OUT="${TMPDIR:-/tmp}/baritone-test"
OBJ="$OUT/obj"
MODOBJ="$OUT/modobj"

if [ -n "${ZIG:-}" ] && [ -x "$ZIG" ]; then CXX=("$ZIG" c++)
elif command -v zig >/dev/null 2>&1; then CXX=("zig" c++)
elif command -v clang++ >/dev/null 2>&1; then CXX=("clang++" -stdlib=libc++)
else CXX=("g++"); fi

export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$OUT/zigcache}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$OUT/zigcache-local}"
export TMPDIR="${TMPDIR:-/tmp}"

if [ "${1:-}" = "--clean" ]; then rm -rf "$OUT"; fi
mkdir -p "$OBJ" "$MODOBJ" "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"

# ---------------------------------------------------------------- core objects
echo "== compiling core =="
find "$SRC/baritone" -name '*.cpp' | sort > "$OUT/srcs.txt"

# Objects are reused, which is fine for speed but has bitten twice: a header edit
# (Baritone.h gaining a member) left stale objects that linked and then crashed. A
# cheap fingerprint of every header catches that without a full dependency walk.
FINGERPRINT="$(find "$SRC" \( -name '*.h' -o -name '*.hpp' \) | sort | xargs cat | cksum)"
if [ -f "$OUT/headers.fingerprint" ] && \
   [ "$(cat "$OUT/headers.fingerprint")" != "$FINGERPRINT" ]; then
    echo "  headers changed, rebuilding"
    rm -f "$OBJ"/*.o
fi

# A stale object once linked cleanly and segfaulted at runtime (a process was added to
# Baritone's member list; an old Baritone.o still had the smaller layout). Cheap
# per-object mtime/dependency staleness cannot see that, because the header is older
# than the object. So: rebuilding is the default, and reusing objects is opt-in.
if [ "${INCREMENTAL:-0}" != "1" ]; then
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
MODFLAGS=(-std=c++23 -O1 -Wno-date-time -Wno-unused-result -DFMT_HEADER_ONLY
          -I"$SRC" -I"$ROOT" -I"$ROOT/tests/stubs"
          -I"$ROOT/third_party/entt/src" -I"$ROOT/third_party/fmt/include"
          -I"$ROOT/reference/preloader-android/include")
modfail=0
for m in "${MODS[@]}"; do
    n=$(echo "$m" | sed 's|/|_|g; s|\.cpp$|.o|')
    if ! "${CXX[@]}" "${MODFLAGS[@]}" -c "$SRC/$m" -o "$MODOBJ/$n" 2>"$MODOBJ/$n.log"; then
        echo "compile failed: $m"; head -5 "$MODOBJ/$n.log"; modfail=1
    fi
done
[ $modfail -ne 0 ] && exit 1
echo "mod layer: ${#MODS[@]} files"

# ----------------------------------------------------------------------- tests
status=0
for test in "$ROOT"/tests/*_harness.cpp; do
    name=$(basename "$test" .cpp)
    case "$name" in
        mine_command_harness|breaker_harness)
            # needs the mod layer + preloader stubs
            extra=("$MODOBJ"/*.o "$OBJ"/*.o "$ROOT/tests/stubs/preloader_lite.cpp"
                   "$ROOT/tests/stubs/ubsan_lite.cpp")
            flags=("${MODFLAGS[@]}")
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
        echo "build failed:"; head -8 "$OUT/$name.build.log"; status=1; continue
    fi
    "$OUT/$name" || status=1
done

echo
if [ $status -eq 0 ]; then echo "TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $status
