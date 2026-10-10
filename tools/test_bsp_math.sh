#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-tests
${CC:-cc} -std=c11 -O1 -g -DD64_PC -DF3DEX_GBI -fwrapv -fno-strict-aliasing \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize=shift,signed-integer-overflow \
  -Isrc/doom64 -Isrc/port -Isrc/port/include -Isrc/gfx \
  tests/test_bsp_math.c src/doom64/m_fixed.c src/doom64/r_phase1.c src/doom64/tables.c \
  -Wl,--gc-sections -Wl,--wrap=FixedDiv2 -lm -o build-tests/test_bsp_math
build-tests/test_bsp_math
