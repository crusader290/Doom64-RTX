#!/bin/sh
# Builds release archives into releases/ from existing build trees:
#   build      Linux x86-64 Release build (cmake -DCMAKE_BUILD_TYPE=Release)
#   build-win  Windows x86-64 MinGW build (cmake/mingw-w64-x86_64.cmake)
# Usage: tools/make_release.sh <version> [path to libSDL3.so.0]
set -e
V="$1"
SDLSO="${2:-build/_deps/sdl3-build/libSDL3.so.0}"
[ -n "$V" ] || { echo "usage: $0 <version> [libSDL3.so.0]"; exit 1; }
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TMP=$(mktemp -d)
L="$TMP/doom64rtx-$V-linux-x86_64"
W="$TMP/doom64rtx-$V-windows-x86_64"
mkdir -p "$L" "$W" releases
cp build/doom64rtx "$L/"
cp -L "$SDLSO" "$L/libSDL3.so.0"
strip "$L/doom64rtx" "$L/libSDL3.so.0"
patchelf --set-rpath '$ORIGIN' "$L/doom64rtx"
cp build-win/doom64rtx.exe build-win/SDL3.dll "$W/"
x86_64-w64-mingw32-strip "$W/doom64rtx.exe" "$W/SDL3.dll"
# Bundled resource packs (auto-loaded from packs/): the doom64-rt sprite packs that
# replace Doom 64 lumps, and the doom64-rt RT materials as one pk3.
python3 tools/pack_visuals.py "$TMP/doom64rtx-visuals.pk3"
for d in "$L" "$W"; do
  cp -r assets "$d/"
  mkdir -p "$d/packs"
  cp "$TMP/doom64rtx-visuals.pk3" "$d/packs/"
  cp tools/release/PACKS_CREDITS.txt "$d/packs/CREDITS.txt"
done
for d in "$L" "$W"; do
  cp README.md LICENSE "$d/"
  mkdir -p "$d/docs"
  cp docs/ADDON_COMPATIBILITY.md "$d/docs/"
  cp tools/release/doom64rtx.ini "$d/"
  sed "s/@VERSION@/$V/; s/@COMMIT@/$(git rev-parse --short HEAD)/" tools/release/RELEASE_NOTES.txt > "$d/RELEASE_NOTES.txt"
done
(cd "$TMP" && tar czf "$ROOT/releases/doom64rtx-$V-linux-x86_64.tar.gz" "doom64rtx-$V-linux-x86_64")
python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', sys.argv[2], sys.argv[3])" \
  "$ROOT/releases/doom64rtx-$V-windows-x86_64" "$TMP" "doom64rtx-$V-windows-x86_64"
rm -rf "$TMP"
ls -la releases
