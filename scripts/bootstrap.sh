#!/usr/bin/env bash
# Reproducible bootstrap for the headless engine and its regression tests.
#
#   ./scripts/bootstrap.sh            # fetch/extract pinned dependencies
#   ./scripts/bootstrap.sh --check    # verify an existing bootstrap, no network
#   ./scripts/bootstrap.sh --offline  # use only third_party/cache
#
# All generated files live under ignored third_party/. The only hosts contacted are
# codeload.github.com and files.pythonhosted.org, both pinned by SHA-256 below.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY="${BOOTSTRAP_ROOT:-$ROOT/third_party}"
# BOOTSTRAP_ROOT / BOOTSTRAP_CACHE let a test bootstrap into a scratch directory
# without touching the real third_party tree (and without network surprises).
CACHE="${BOOTSTRAP_CACHE:-$THIRD_PARTY/cache}"
TOOLCHAIN="$THIRD_PARTY/toolchain"

ENTT_VERSION="3.15.0"
ENTT_ARCHIVE="entt-${ENTT_VERSION}.tar.gz"
ENTT_URL="https://codeload.github.com/skypjack/entt/tar.gz/refs/tags/v${ENTT_VERSION}"
ENTT_SHA256="01466fcbf77618a79b62891510c0bbf25ac2804af5751c84982b413852234d66"

FMT_VERSION="11.2.0"
FMT_ARCHIVE="fmt-${FMT_VERSION}.tar.gz"
FMT_URL="https://codeload.github.com/fmtlib/fmt/tar.gz/refs/tags/${FMT_VERSION}"
FMT_SHA256="bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af"

ZIG_VERSION="0.15.1"
ZIG_WHEEL="ziglang-${ZIG_VERSION}-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl"
ZIG_URL="https://files.pythonhosted.org/packages/30/a0/8aabb5f4e0862340ebb4d86f614567a5492e4e1ac09639c9c759404babe1/${ZIG_WHEEL}"
ZIG_SHA256="4e45994a0e608d9b16ecad255698f5557a2e24de0bd7ba9efb156ab3f3683d9a"

CMAKE_VERSION="3.31.6"
CMAKE_WHEEL="cmake-${CMAKE_VERSION}-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.whl"
CMAKE_URL="https://files.pythonhosted.org/packages/1e/52/85550dfcadca90b59809a1225461bfaadfcbbcc8fe62fa24f75edbe6e0b1/${CMAKE_WHEEL}"
CMAKE_SHA256="2297e9591307d9c61e557efe737bcf4d7c13a30f1f860732f684a204fee24dca"

MODE="install"
OFFLINE=0
FORCE=0
for arg in "$@"; do
    case "$arg" in
        --check) MODE="check" ;;
        --offline) OFFLINE=1 ;;
        --force) FORCE=1 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "bootstrap: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

fail() { echo "bootstrap: $*" >&2; exit 1; }
sha256() { sha256sum "$1" | awk '{print $1}'; }
verify() {
    local path="$1" expected="$2" label="$3"
    [ -f "$path" ] || fail "missing $label: $path"
    local actual; actual="$(sha256 "$path")"
    [ "$actual" = "$expected" ] || fail "SHA-256 mismatch for $label: expected $expected, got $actual"
}

require_host() {
    case "$(uname -s):$(uname -m)" in
        Linux:x86_64|Linux:amd64) ;;
        *) fail "pinned toolchain wheels target Linux x86_64; this host is $(uname -s) $(uname -m). Use a C++23 compiler with libc++ and <format> manually." ;;
    esac
}

fetch() {
    local name="$1" url="$2" expected="$3"
    local path="$CACHE/$name"
    if [ -f "$path" ]; then
        verify "$path" "$expected" "$name"
        printf '%s' "$path"
        return
    fi
    [ "$OFFLINE" -eq 0 ] || fail "offline bootstrap cannot fetch missing $name"
    mkdir -p "$CACHE"
    local tmp="$path.part.$$"
    echo "bootstrap: fetching $name" >&2
    curl --fail --silent --show-error --location --retry 3 --output "$tmp" "$url"
    verify "$tmp" "$expected" "$name"
    mv "$tmp" "$path"
    printf '%s' "$path"
}

stamp_ok() {
    local stamp="$1" expected="$2"
    [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$expected" ]
}

extract_tarball() {
    local archive="$1" expected="$2" dest="$3" marker="$4" label="$5"
    if [ "$FORCE" -eq 0 ] && stamp_ok "$dest/.bootstrap-sha256" "$expected" && [ -f "$dest/$marker" ]; then
        return
    fi
    [ "$MODE" = "install" ] || fail "$label is not extracted"
    echo "bootstrap: extracting $label" >&2
    rm -rf "$dest"
    mkdir -p "$dest"
    tar -xzf "$archive" -C "$dest" --strip-components=1
    [ -f "$dest/$marker" ] || fail "archive did not contain $marker"
    printf '%s' "$expected" > "$dest/.bootstrap-sha256"
}

extract_wheel() {
    local archive="$1" expected="$2" dest="$3" marker="$4" label="$5"
    if [ "$FORCE" -eq 0 ] && stamp_ok "$dest/.bootstrap-sha256" "$expected" && [ -f "$dest/$marker" ]; then
        return
    fi
    [ "$MODE" = "install" ] || fail "$label is not extracted"
    echo "bootstrap: extracting $label" >&2
    rm -rf "$dest"
    mkdir -p "$dest"
    python3 -m zipfile -e "$archive" "$dest"
    [ -f "$dest/$marker" ] || fail "wheel did not contain $marker"
    chmod +x "$dest/$marker"
    printf '%s' "$expected" > "$dest/.bootstrap-sha256"
}

require_host
mkdir -p "$THIRD_PARTY" "$TOOLCHAIN"

if [ "$MODE" = "check" ]; then
    OFFLINE=1
    verify "$CACHE/$ENTT_ARCHIVE" "$ENTT_SHA256" "$ENTT_ARCHIVE"
    verify "$CACHE/$FMT_ARCHIVE" "$FMT_SHA256" "$FMT_ARCHIVE"
    verify "$CACHE/$ZIG_WHEEL" "$ZIG_SHA256" "$ZIG_WHEEL"
    verify "$CACHE/$CMAKE_WHEEL" "$CMAKE_SHA256" "$CMAKE_WHEEL"
    extract_tarball "$CACHE/$ENTT_ARCHIVE" "$ENTT_SHA256" "$THIRD_PARTY/entt" "src/entt/entt.hpp" "EnTT"
    extract_tarball "$CACHE/$FMT_ARCHIVE" "$FMT_SHA256" "$THIRD_PARTY/fmt" "include/fmt/format.h" "fmt"
    extract_wheel "$CACHE/$ZIG_WHEEL" "$ZIG_SHA256" "$TOOLCHAIN/ziglang" "ziglang/zig" "Zig"
    extract_wheel "$CACHE/$CMAKE_WHEEL" "$CMAKE_SHA256" "$TOOLCHAIN/cmake" "cmake/data/bin/cmake" "CMake"
else
    entt_archive="$(fetch "$ENTT_ARCHIVE" "$ENTT_URL" "$ENTT_SHA256")"
    fmt_archive="$(fetch "$FMT_ARCHIVE" "$FMT_URL" "$FMT_SHA256")"
    zig_archive="$(fetch "$ZIG_WHEEL" "$ZIG_URL" "$ZIG_SHA256")"
    cmake_archive="$(fetch "$CMAKE_WHEEL" "$CMAKE_URL" "$CMAKE_SHA256")"
    extract_tarball "$entt_archive" "$ENTT_SHA256" "$THIRD_PARTY/entt" "src/entt/entt.hpp" "EnTT"
    extract_tarball "$fmt_archive" "$FMT_SHA256" "$THIRD_PARTY/fmt" "include/fmt/format.h" "fmt"
    extract_wheel "$zig_archive" "$ZIG_SHA256" "$TOOLCHAIN/ziglang" "ziglang/zig" "Zig"
    extract_wheel "$cmake_archive" "$CMAKE_SHA256" "$TOOLCHAIN/cmake" "cmake/data/bin/cmake" "CMake"
fi

ZIG="$TOOLCHAIN/ziglang/ziglang/zig"
CMAKE="$TOOLCHAIN/cmake/cmake/data/bin/cmake"
[ -x "$ZIG" ] || fail "Zig executable missing: $ZIG"
[ -x "$CMAKE" ] || fail "CMake executable missing: $CMAKE"

ENV_TEXT="$(cat <<EOF
# Generated by scripts/bootstrap.sh. Source this file; do not edit.
export ZIG="$ZIG"
export CMAKE="$CMAKE"
export ENTT_DIR="$THIRD_PARTY/entt"
export FMT_DIR="$THIRD_PARTY/fmt"
export ZIG_GLOBAL_CACHE_DIR="\${ZIG_GLOBAL_CACHE_DIR:-$THIRD_PARTY/zig-global-cache}"
export ZIG_LOCAL_CACHE_DIR="\${ZIG_LOCAL_CACHE_DIR:-$THIRD_PARTY/zig-local-cache}"
export PATH="$TOOLCHAIN/cmake/cmake/data/bin:\$PATH"
EOF
)"
if [ "$MODE" = "install" ]; then
    # Only replace the file when the content changed: --check must be read-only and a
    # repeated install must not touch mtimes for no reason.
    if [ ! -f "$THIRD_PARTY/env.sh" ] || [ "$(cat "$THIRD_PARTY/env.sh")" != "$ENV_TEXT" ]; then
        printf '%s\n' "$ENV_TEXT" > "$THIRD_PARTY/env.sh"
    fi
elif [ ! -f "$THIRD_PARTY/env.sh" ]; then
    fail "env.sh is missing; run bootstrap without --check"
fi

# A compiler probe is more useful than a version banner: std::format and the exact
# EnTT/fmt headers are what the source tree actually needs.
PROBE_DIR="$(mktemp -d "$THIRD_PARTY/probe.XXXXXX")"
trap 'rm -rf "$PROBE_DIR"' EXIT
cat > "$PROBE_DIR/probe.cpp" <<'EOF'
#define FMT_HEADER_ONLY
#include <format>
#include <fmt/format.h>
#include <entt/entity/sparse_set.hpp>
#include <cstdio>
int main() {
    std::puts(std::format("cxx23={} fmt={}", 1, fmt::format("{}", 2)).c_str());
    return 0;
}
EOF
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$THIRD_PARTY/zig-global-cache}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$THIRD_PARTY/zig-local-cache}"
mkdir -p "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"
"$ZIG" c++ -std=c++23 -O0 \
    -I"$THIRD_PARTY/entt/src" -I"$THIRD_PARTY/fmt/include" \
    "$PROBE_DIR/probe.cpp" -o "$PROBE_DIR/probe"
[ "$("$PROBE_DIR/probe")" = "cxx23=1 fmt=2" ] || fail "C++23 compiler probe produced unexpected output"
"$CMAKE" --version | head -1 >/dev/null

echo "BOOTSTRAP_OK zig=$ZIG_VERSION cmake=$CMAKE_VERSION entt=$ENTT_VERSION fmt=$FMT_VERSION"
