#!/usr/bin/env bash
# tools/smoke.sh - headless runtime test of build/doom64rtx (needs a Doom 64 ROM,
# never in the repo: set D64_ROM, or put one in ../run next to the repo or the cwd).
#
#   tools/smoke.sh [options]
#     --frames N        run N frames then exit (default 600)
#     --press LIST      D64_PRESS list "frame:hexmask[/len],..." (pad high 16 bits:
#                       1000 START, 8000 A, 4000 B, 0400 down, 0800 up)
#     --shots LIST      frames to screenshot (shot_NNNN.bmp, and .png if PIL exists)
#     --renderer R      gl (default) | vulkan | rt
#     --wav             also write the audio to out.wav
#     --out DIR         output directory (default build-logs/smoke)
#     --set K V         any doom64rtx.ini setting for this run (repeatable)
#     --newgame         start MAP01 on the default skill before --press/--shots
#                       (frames below are relative: shots at N are at N+500)
#
# Known timings (D64_FIXED_TIMESTEP, 30 fps): the title demo runs from start;
# any pad press at frame >= 300 exits it to the title menu (~frame 350); with
# --newgame the level is running at frame ~500.
#
# Prints one line: PASS/FAIL/SKIPPED with the reason. Exit 0 pass, 1 fail,
# 5 skipped (no ROM / no display).
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
FRAMES=600; PRESS=""; SHOTS=""; REND=gl; WAV=0; OUT="$ROOT/build-logs/smoke"; SETS=(); NEWGAME=0
while [ $# -gt 0 ]; do
  case "$1" in
    --frames) FRAMES=$2; shift ;;
    --press) PRESS=$2; shift ;;
    --shots) SHOTS=$2; shift ;;
    --renderer) REND=$2; shift ;;
    --wav) WAV=1 ;;
    --out) OUT=$2; shift ;;
    --set) SETS+=(-set "$2" "$3"); shift 2 ;;
    --newgame) NEWGAME=1 ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

EXE="$ROOT/build/doom64rtx"
[ -x "$EXE" ] || { echo "SKIPPED: $EXE not built (tools/check.sh linux)"; exit 5; }
ROM="${D64_ROM:-}"
if [ -z "$ROM" ]; then
  for d in "$PWD" "$ROOT/../run" "$HOME/run"; do
    f=$(ls "$d"/*.z64 "$d"/*.n64 "$d"/*.v64 2>/dev/null | head -1)
    [ -n "$f" ] && { ROM=$f; break; }
  done
fi
[ -n "$ROM" ] && [ -f "$ROM" ] || { echo "SKIPPED: no Doom 64 ROM (set D64_ROM)"; exit 5; }

if [ -z "${DISPLAY:-}" ] || ! xdpyinfo -display "${DISPLAY}" >/dev/null 2>&1; then
  export DISPLAY=:99
  if ! pgrep -f "Xvfb :99" >/dev/null 2>&1; then
    command -v Xvfb >/dev/null 2>&1 || { echo "SKIPPED: no display and no Xvfb"; exit 5; }
    (Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 &)
    sleep 2
  fi
fi

if [ $NEWGAME -eq 1 ]; then
  shift_list() { # add 500 to every frame number in a "f:..." or "f,f" list
    echo "$1" | tr ',' '\n' | awk -F: 'NF{ $1=$1+500; print }' OFS=: | paste -sd, -
  }
  [ -n "$PRESS" ] && PRESS=$(shift_list "$PRESS")
  [ -n "$SHOTS" ] && SHOTS=$(shift_list "$SHOTS")
  PRESS="300:0400/2,380:8000/2,440:8000/2${PRESS:+,$PRESS}"
  FRAMES=$((FRAMES + 500))
fi

mkdir -p "$OUT"
ARGS=(-nosound -rom "$ROM")
case "$REND" in
  gl) ARGS=(-gl -rom "$ROM") ;;
  vulkan) ARGS=(-vulkan -nort -rom "$ROM") ;;
  rt) ARGS=(-vulkan -rt -rom "$ROM") ;;
esac
[ $WAV -eq 0 ] && ARGS+=(-nosound)
ENVV=(D64_HEADLESS=1 D64_FIXED_TIMESTEP=1 D64_QUIT_AT="$FRAMES")
[ -n "$PRESS" ] && ENVV+=(D64_PRESS="$PRESS")
[ -n "$SHOTS" ] && ENVV+=(D64_SHOTS="$SHOTS")
[ $WAV -eq 1 ] && ENVV+=(D64_WAVOUT="$OUT/out.wav")

start=$(date +%s)
(cd "$OUT" && env "${ENVV[@]}" timeout $((FRAMES / 5 + 120)) "$EXE" "${ARGS[@]}" "${SETS[@]}" > run.txt 2>&1)
rc=$?
secs=$(( $(date +%s) - start ))
LOGF=$(ls -t "$HOME"/.local/share/Doom64RTX/Doom64RTX/doom64rtx.log 2>/dev/null | head -1)

if [ -n "$SHOTS" ] && python3 -c "import PIL" 2>/dev/null; then
  for b in "$OUT"/shot_*.bmp; do
    [ -f "$b" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$b" "${b%.bmp}.png"
  done
fi

if [ $rc -eq 0 ] && grep -q "D64_QUIT_AT reached" "$OUT/run.txt"; then
  echo "PASS: $FRAMES frames ($REND) in ${secs}s; output in ${OUT#$ROOT/}"
  exit 0
fi
echo "FAIL: exit $rc after ${secs}s ($REND); see ${OUT#$ROOT/}/run.txt"
[ -n "$LOGF" ] && grep -A12 "==== CRASH" "$LOGF" | head -14
grep -iE "error|fatal" "$OUT/run.txt" | grep -v XDG_RUNTIME | head -5
exit 1
