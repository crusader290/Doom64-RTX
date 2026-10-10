#!/usr/bin/env bash
# Called by build.bat in its downloaded MINGW64 shell.
set -euo pipefail
cd "$(cygpath -u "$D64_BUILD_ROOT")"
cmake -S . -B build-native-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-win --parallel
# CMake copies SDL3.dll; SDL's optional shared runtime dependencies live here.
for dll in libgcc_s_seh-1.dll libwinpthread-1.dll libiconv-2.dll; do
  if [[ -f "/mingw64/bin/$dll" ]]; then cp "/mingw64/bin/$dll" build-native-win/; fi
done
