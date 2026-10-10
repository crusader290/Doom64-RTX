#!/usr/bin/env bash
# Download missing dependencies and build Linux and Windows (MinGW) by default.
set -euo pipefail
cd "$(dirname "$0")"
export D64_DEPS="${D64_DEPS:-$PWD/build-deps}"
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
  echo 'Usage: ./build.sh [linux|win|all|null] [--clean] [--force]'
  echo 'Defaults to Linux + Windows. Debian/Ubuntu dependency installation may require sudo.'
  exit 0
fi
[[ -f "$HOME/.cargo/env" ]] && source "$HOME/.cargo/env"
bash tools/setup_env.sh
[[ -f "$HOME/.cargo/env" ]] && source "$HOME/.cargo/env"
if [[ $# == 0 ]]; then set -- linux win; fi
bash tools/check.sh "$@"
