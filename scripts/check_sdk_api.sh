#!/usr/bin/env bash
# Type-checks every source file that touches the preloader SDK.
#
# This catches the class of error that would otherwise only surface on device:
# a wrong SDK signature, a constexpr violation, a missing include. It needs the
# preloader headers, so it is skipped (not failed) when they are absent.
#
#   PRELOADER_INCLUDE=/path/to/preloader/include ./scripts/check_sdk_api.sh
#
# What it does NOT prove: that the code links, that the hooks work, that any
# address is correct. Those need a real NDK build and a device.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" || exit 1

sdk="${PRELOADER_INCLUDE:-}"
if [ -z "$sdk" ]; then
    for candidate in /tmp/preloader/include "$root/.deps/preloader/include" "$root/third_party/preloader/include"; do
        if [ -d "$candidate/pl" ]; then sdk="$candidate"; break; fi
    done
fi

if [ ! -d "$sdk/pl" ]; then
    echo "SKIP: preloader headers not found (set PRELOADER_INCLUDE)."
    exit 0
fi

echo "preloader headers: $sdk"

# Host stubs for Android-only headers (jni.h, android/log.h, EGL/egl.h, fmt).
stub="$(mktemp -d)"
mkdir -p "$stub/EGL" "$stub/android" "$stub/fmt"
cat > "$stub/jni.h" <<'EOF'
#pragma once
typedef struct _JavaVM JavaVM;
typedef struct _JNIEnv JNIEnv;
EOF
cat > "$stub/android/log.h" <<'EOF'
#pragma once
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
extern "C" int __android_log_print(int, const char*, const char*, ...);
EOF
cat > "$stub/EGL/egl.h" <<'EOF'
#pragma once
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef unsigned int EGLBoolean;
#define EGL_NO_CONTEXT ((void*)0)
#define EGL_FALSE 0
#define EGL_TRUE 1
extern "C" { EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface); void* eglGetCurrentContext(void); }
EOF
cat > "$stub/fmt/format.h" <<'EOF'
#pragma once
#include <string>
#include <string_view>
#include <type_traits>
namespace fmt {
using string_view = std::string_view;
template <typename... Args> struct format_arg_store {};
template <typename... Args> inline format_arg_store<std::decay_t<Args>...> make_format_args(Args&&...) { return {}; }
template <typename... Args> inline std::string vformat(string_view f, format_arg_store<Args...>) { return std::string(f); }
}
EOF
cat > "$stub/fmt/std.h" <<'EOF'
#pragma once
#include <fmt/format.h>
#include <filesystem>
#include <string>
EOF

cxx="${CXX:-g++}"
failures=0
checked=0

for file in src/main.cpp src/core/*.cpp src/platform/*.cpp src/game/*.cpp src/features/*.cpp; do
    [ -e "$file" ] || continue
    checked=$((checked + 1))
    # -D poisoning reproduces Android's <dlfcn.h>, which declares neither dlinfo nor
    # RTLD_DI_LINKMAP (both glibc extensions). Shim headers cannot do this: glibc's
    # <bits/dlfcn.h> is force-included and beats any -I stub. Defining the name to an
    # impossible token turns a glibc-only call into a host compile error -- the check
    # that was missing when RTLD_DI_LINKMAP reached CI.
    if output=$("$cxx" -std=c++20 -fsyntax-only \
            -DRTLD_DI_LINKMAP=CAMFACE_NOT_ON_ANDROID \
            -I"$stub" -I"$sdk" -Iinclude -Isrc "$file" 2>&1); then
        printf '  ok    %s\n' "$file"
    else
        printf '  FAIL  %s\n' "$file"
        printf '%s\n' "$output" | head -20 | sed 's/^/        /'
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo "FAILED: $failures of $checked file(s) do not compile against the SDK"
    exit 1
fi
echo "ok: $checked file(s) compile against the preloader SDK API"
