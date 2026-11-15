#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/baritone-android"
BUILD="${BUILD_DIR:-$ROOT/build}"
OBJ="$BUILD/obj"
OUT="${OUT:-$BUILD/libbaritone.so}"
CXX="${CXX:-aarch64-linux-android-clang++}"
CXXFLAGS=(-std=c++23 -fPIC -O2 -DNDEBUG -ffunction-sections -fdata-sections -w -DFMT_HEADER_ONLY
          -fvisibility=hidden -fvisibility-inlines-hidden
          -I "$SRC/src/android/src" -I "$SRC/src" -I "$SRC/third_party/entt/src"
          -I "$SRC/third_party/fmt/include" -I "$ROOT/preloader-android/include")

mkdir -p "$OBJ"
cd "$SRC"

compiled=0
while IFS= read -r src; do
    name=$(printf '%s' "$src" | tr '/' '_')
    obj="$OBJ/$name.o"
    dep="$OBJ/$name.d"
    stale=0
    [ ! -f "$obj" ] && stale=1
    [ "$src" -nt "$obj" ] && stale=1
    if [ ! -f "$dep" ]; then
        stale=1
    else
        for d in $(sed -e 's/\\$//' "$dep" | sed -e 's/^[^:]*://' | tr ' ' '\n'); do
            [ -z "$d" ] && continue
            [ -e "$d" ] && [ "$d" -nt "$obj" ] && stale=1
        done
    fi
    if [ "$stale" -eq 1 ]; then
        echo "compile $src"
        "$CXX" "${CXXFLAGS[@]}" -MMD -MP -MF "$dep" -c "$src" -o "$obj"
        compiled=$((compiled + 1))
    fi
done < <(find src -name '*.cpp' | sort)

"$CXX" -shared -Wl,--unresolved-symbols=ignore-all -Wl,-z,max-page-size=16384 -Wl,--no-as-needed -Wl,--gc-sections \
    -o "$OUT" "$OBJ"/*.o -lc++_shared -ldl -lm

echo "BUILD_OK ($compiled sources recompiled) -> $OUT"
