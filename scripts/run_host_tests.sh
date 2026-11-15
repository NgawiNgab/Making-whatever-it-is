#!/usr/bin/env bash
# Compiles and runs the host tests with any C++20 compiler.
#
# This is the verification that is possible without a device or the Android
# toolchain: the pattern parser, the fact resolver's decision rules, and the
# resolution report. It does not and cannot prove anything about the game.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

cxx="${CXX:-g++}"
out="$(mktemp -d)/camface_tests"

echo "compiling host tests with $cxx ..."
"$cxx" -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter -Iinclude \
    tests/test_main.cpp \
    src/core/Pattern.cpp \
    src/core/Facts.cpp \
    src/core/BuildRegistry.cpp \
    data/1.26.62.cpp \
    -o "$out"

echo "running ..."
"$out"
