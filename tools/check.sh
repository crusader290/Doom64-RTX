#!/usr/bin/env bash
# tools/check.sh - single build/validation entry point.
#
#   tools/check.sh [linux|win|null|all] [--clean] [--force]
#
# - verifies the tools the target needs (exit 3 = environment problem)
# - configures once, then builds incrementally
# - keeps the full log in build-logs/<target>.log
# - prints a compact summary: the first errors with context, error counts,
#   and a failure class: env | dependency | code | link | unknown
# - refuses to re-run a build that failed with the same first error while the
#   sources are unchanged (exit 4); --force overrides. D64_MAX_SAME_FAILURES
#   (default 3) warns when one error survives that many different attempts.
# - appends one line per run to build-logs/history.tsv
#
# Exit codes: 0 ok, 1 build failed, 2 usage, 3 environment, 4 repeat refused.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TARGETS=()
CLEAN=0
FORCE=0
for a in "$@"; do
  case "$a" in
    linux|win|null) TARGETS+=("$a") ;;
    all) TARGETS+=(linux win null) ;;
    --clean) CLEAN=1 ;;
    --force) FORCE=1 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "unknown argument: $a" >&2; exit 2 ;;
  esac
done
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(linux)
MAXSAME=${D64_MAX_SAME_FAILURES:-3}
LOGDIR="$ROOT/build-logs"
mkdir -p "$LOGDIR"
REV=$(git rev-parse --short HEAD 2>/dev/null || echo none)
# fingerprint of the inputs: HEAD + uncommitted changes
INPUTS=$( { git rev-parse HEAD; git diff HEAD -- . ':!build-logs' 2>/dev/null; } | sha1sum | cut -c1-12)

need() { command -v "$1" >/dev/null 2>&1 || MISSING+=("$1"); }

builddir() {
  case "$1" in
    linux) echo build ;;
    win) echo build-win ;;
    null) echo build-null ;;
  esac
}

configure_args() {
  local sdl="${SDL3_DIR:-${D64_DEPS:-$ROOT/../deps}/sdl3-linux/lib/cmake/SDL3}"
  case "$1" in
    linux)
      echo "-G Ninja -DCMAKE_BUILD_TYPE=Release"
      [ -f "$sdl/SDL3Config.cmake" ] && echo "-DSDL3_DIR=$sdl" ;;
    win) echo "-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake" ;;
    null)
      echo "-G Ninja -DCMAKE_BUILD_TYPE=Release -DD64_NULL_RENDERER=ON"
      [ -f "$sdl/SDL3Config.cmake" ] && echo "-DSDL3_DIR=$sdl" ;;
  esac
}

classify() { # log -> class
  local log=$1
  if grep -qE "command not found|No CMAKE_[A-Z]+_COMPILER could be found|could not find (any instance|cargo)|failed to (connect|download)|Could not resolve host|network|unable to access 'https" "$log"; then
    echo env
  elif grep -qE "Could not find a package configuration file|SDL3 not found|FetchContent|failed to select a version|no matching package|can't find crate|target may not be installed" "$log"; then
    echo dependency
  elif grep -qE "(error|Error)(\[E[0-9]+\])?:|: error:" "$log"; then
    if grep -qE "undefined reference|unresolved external|ld returned|multiple definition" "$log" &&
       ! grep -qE "\.(c|h|rs):[0-9]+(:[0-9]+)?: error" "$log"; then
      echo link
    else
      echo code
    fi
  elif grep -qE "undefined reference|ld returned" "$log"; then
    echo link
  else
    echo unknown
  fi
}

first_errors() { # log -> first 3 distinct diagnostics (+ source/caret lines)
  local pat='[A-Za-z0-9_./-]+\.(c|h|rs|cmake|txt):[0-9]+(:[0-9]+)?: (fatal )?error|^error(\[E[0-9]+\])?:|undefined reference|CMake Error'
  local lines
  lines=$(grep -nE "$pat" "$1" | awk -F: '{k=$0; sub(/^[0-9]+:/,"",k); if(!seen[k]++) print $1}' | head -3)
  if [ -z "$lines" ]; then
    echo "  (no compiler diagnostic found; last lines of the log)"
    tail -n 12 "$1" | cut -c1-200 | sed "s|$ROOT/||g"
    return
  fi
  for n in $lines; do
    sed -n "${n},$((n+3))p" "$1" | grep -vE '^(/usr/bin/|\[[0-9]+/[0-9]+\]|FAILED:|ninja:)' |
      cut -c1-200 | sed "s|$ROOT/||g"
    echo "  --"
  done
}

summary_md() {
  [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >> "$GITHUB_STEP_SUMMARY"
}

overall=0
for T in "${TARGETS[@]}"; do
  MISSING=()
  need cmake; need ninja; [ "$T" = null ] || need cargo
  case "$T" in
    linux|null) need cc ;;
    win) need x86_64-w64-mingw32-gcc
         rustup target list --installed 2>/dev/null | grep -q x86_64-pc-windows-gnu || MISSING+=("rust target x86_64-pc-windows-gnu") ;;
  esac
  LOG="$LOGDIR/$T.log"
  if [ ${#MISSING[@]} -gt 0 ]; then
    echo "[$T] ENV: missing ${MISSING[*]} - run tools/setup_env.sh"
    printf '%s\t%s\t%s\t%s\tenv\t-\tmissing %s\n' "$(date -u +%FT%TZ)" "$REV" "$INPUTS" "$T" "${MISSING[*]}" >> "$LOGDIR/history.tsv"
    summary_md "**$T**: environment - missing ${MISSING[*]}"
    overall=3
    continue
  fi

  # repeat guard: same inputs + last run failed -> do not rebuild blindly
  last=$(awk -F'\t' -v t="$T" '$4==t' "$LOGDIR/history.tsv" 2>/dev/null | tail -1)
  if [ $FORCE -eq 0 ] && [ -n "$last" ]; then
    lin=$(echo "$last" | cut -f3); lres=$(echo "$last" | cut -f5)
    if [ "$lin" = "$INPUTS" ] && [ "$lres" != ok ] && [ "$lres" != env ]; then
      echo "[$T] REFUSED: the last build failed ($lres) with identical sources."
      echo "    first error: $(echo "$last" | cut -f7)"
      echo "    Change the code or the hypothesis first, or pass --force. Full log: build-logs/$T.log"
      overall=4
      continue
    fi
  fi

  D=$(builddir "$T")
  [ $CLEAN -eq 1 ] && rm -rf "$D"
  start=$(date +%s)
  (
    echo "# tools/check.sh $T  rev=$REV inputs=$INPUTS  $(date -u +%FT%TZ)"
    if [ ! -f "$D/CMakeCache.txt" ]; then
      # shellcheck disable=SC2046
      cmake -S . -B "$D" $(configure_args "$T") || exit 1
    fi
    cmake --build "$D" || exit 1
  ) > "$LOG" 2>&1
  rc=$?
  secs=$(( $(date +%s) - start ))
  if [ $rc -eq 0 ]; then
    warns=$(grep -c "warning:" "$LOG")
    echo "[$T] OK in ${secs}s ($warns warnings) rev=$REV  log: build-logs/$T.log"
    printf '%s\t%s\t%s\t%s\tok\t%s\t-\n' "$(date -u +%FT%TZ)" "$REV" "$INPUTS" "$T" "$secs" >> "$LOGDIR/history.tsv"
    summary_md "**$T**: OK in ${secs}s ($warns warnings)"
    continue
  fi
  class=$(classify "$LOG")
  nerr=$(grep -cE ": error|error(\[E[0-9]+\])?:|undefined reference" "$LOG")
  first=$(grep -m1 -E "\.(c|h|rs|cmake|txt):[0-9]+(:[0-9]+)?: (fatal )?error|^error(\[E[0-9]+\])?:|undefined reference|CMake Error" "$LOG" | sed "s|$ROOT/||g" | cut -c1-200)
  [ -z "$first" ] && first=$(grep -m1 -E "error|Error" "$LOG" | sed "s|$ROOT/||g" | cut -c1-200)
  sig=$(echo "$first" | sed -E 's/[0-9]+//g' | sha1sum | cut -c1-10)
  echo "[$T] FAILED ($class) in ${secs}s, $nerr error lines. Full log: build-logs/$T.log"
  echo "first errors:"
  first_errors "$LOG" | sed 's/^/  /'
  same=$(awk -F'\t' -v t="$T" -v s="$sig" '$4==t && $8==s' "$LOGDIR/history.tsv" 2>/dev/null | wc -l)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date -u +%FT%TZ)" "$REV" "$INPUTS" "$T" "$class" "$secs" "$first" "$sig" >> "$LOGDIR/history.tsv"
  if [ "$same" -ge $((MAXSAME - 1)) ]; then
    echo "WARNING: this first error has now failed $((same + 1)) attempts. Stop and re-diagnose (root cause, not symptoms)."
  fi
  [ "$class" = env ] && echo "hint: environment problem - run tools/setup_env.sh, do not edit code for this."
  summary_md "**$T**: FAILED ($class): \`$first\`"
  overall=1
done
exit $overall
