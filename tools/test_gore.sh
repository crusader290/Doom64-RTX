#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-tests
${CC:-cc} -std=c11 -O1 -g -DD64_PC -DF3DEX_GBI -fwrapv -fno-strict-aliasing \
  -fsanitize=address,undefined -Isrc/doom64 -Isrc/port -Isrc/port/include -Isrc/gfx \
  tests/test_gore.c -lm -o build-tests/test_gore
build-tests/test_gore
