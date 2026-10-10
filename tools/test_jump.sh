#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
prefix="${D64_DEPS:-$PWD/../deps}/sdl3-linux"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
mkdir -p build-tests
${CC:-cc} -std=c11 -O1 -g -DD64_PC -DF3DEX_GBI -fwrapv -fno-strict-aliasing \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined \
  -Isrc/doom64 -Isrc/port -Isrc/port/include -Isrc/gfx $(pkg-config --cflags sdl3) \
  tests/test_jump.c src/doom64/m_fixed.c $(pkg-config --libs sdl3) -lm -o build-tests/test_jump
LD_LIBRARY_PATH="$prefix/lib:${LD_LIBRARY_PATH:-}" build-tests/test_jump
