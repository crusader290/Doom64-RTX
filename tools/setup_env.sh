#!/usr/bin/env bash
# tools/setup_env.sh - prepare a (cloud) build/test environment. Idempotent:
# every step is skipped when its result is already present.
#
#   tools/setup_env.sh           install what is missing (needs network + root for apt)
#   tools/setup_env.sh --check   report only, < 1 s, no changes (exit 1 if incomplete)
#
# Pinned versions: SDL release-3.2.24 (same tag as CMakeLists.txt FetchContent),
# Rust stable + target x86_64-pc-windows-gnu.
# Prebuilt SDL3 goes to $D64_DEPS (default: deps/ next to the repository); tools/check.sh
# picks it up, which avoids a FetchContent clone + SDL build per fresh tree.
set -u
SDL_TAG=release-3.2.24
DEPS=${D64_DEPS:-$(cd "$(dirname "$0")/../.." && pwd)/deps}
SDL_PREFIX="$DEPS/sdl3-linux"
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

APT_PKGS="cmake ninja-build build-essential pkg-config glslang-tools mingw-w64 patchelf xvfb
mesa-vulkan-drivers libgl1-mesa-dri libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl-dev libgl-dev libdecor-0-dev
libasound2-dev libpulse-dev libudev-dev libdbus-1-dev"
# optional: wine (Windows smoke tests), python3-pil (screenshot conversion)

missing=()
for t in cmake ninja cc cargo glslangValidator x86_64-w64-mingw32-gcc patchelf Xvfb; do
  command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
rustup target list --installed 2>/dev/null | grep -q x86_64-pc-windows-gnu || missing+=("rust:x86_64-pc-windows-gnu")
[ -f "$SDL_PREFIX/lib/cmake/SDL3/SDL3Config.cmake" ] || missing+=("sdl3-prebuilt")
optional=()
command -v wine >/dev/null 2>&1 || optional+=("wine")
python3 -c "import PIL" 2>/dev/null || optional+=("python3-pil")

if [ $CHECK -eq 1 ]; then
  if [ ${#missing[@]} -eq 0 ]; then
    echo "env: ready${optional:+ (optional missing: ${optional[*]})}"
    exit 0
  fi
  echo "env: missing ${missing[*]} - run tools/setup_env.sh${optional:+ (optional missing: ${optional[*]})}"
  exit 1
fi

if [ ${#missing[@]} -eq 0 ] && [ ${#optional[@]} -eq 0 ]; then
  echo "env: ready, nothing to do"
  exit 0
fi

SUDO=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO=sudo
need_apt=0
for m in "${missing[@]}" "${optional[@]}"; do
  case "$m" in rust:*|sdl3-prebuilt|cargo) ;; *) need_apt=1 ;; esac
done
if [ $need_apt -eq 1 ]; then
  echo "env: apt-get install (missing: ${missing[*]} ${optional[*]})"
  $SUDO apt-get update -qq && \
  # shellcheck disable=SC2086
  $SUDO apt-get install -y -qq --no-install-recommends $APT_PKGS wine64 wine python3-pil >/dev/null || \
    echo "env: apt-get failed (network or permissions) - continuing with what is available"
fi

if ! command -v cargo >/dev/null 2>&1; then
  echo "env: installing Rust (rustup)"
  curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal >/dev/null && . "$HOME/.cargo/env"
fi
if command -v rustup >/dev/null 2>&1 && ! rustup target list --installed | grep -q x86_64-pc-windows-gnu; then
  rustup target add x86_64-pc-windows-gnu >/dev/null && echo "env: added rust target x86_64-pc-windows-gnu"
fi

if [ ! -f "$SDL_PREFIX/lib/cmake/SDL3/SDL3Config.cmake" ]; then
  echo "env: building SDL3 $SDL_TAG into $SDL_PREFIX (one time)"
  mkdir -p "$DEPS"
  [ -d "$DEPS/SDL3/.git" ] || git clone -q --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$DEPS/SDL3"
  cmake -S "$DEPS/SDL3" -B "$DEPS/sdl-build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > "$DEPS/sdl-linux.log" 2>&1 &&
  cmake --build "$DEPS/sdl-build-linux" >> "$DEPS/sdl-linux.log" 2>&1 &&
  cmake --install "$DEPS/sdl-build-linux" >> "$DEPS/sdl-linux.log" 2>&1 ||
    { echo "env: SDL3 build failed, see $DEPS/sdl-linux.log (CMake will fall back to FetchContent)"; }
fi

exec "$0" --check
