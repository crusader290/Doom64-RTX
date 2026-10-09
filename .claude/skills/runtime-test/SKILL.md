---
name: runtime-test
description: Run Doom64-RTX headless (Xvfb, Mesa llvmpipe/lavapipe) to verify gameplay, rendering, menus or audio with screenshots, button scripts, traces or WAV capture. Use after changing runtime behaviour.
---
# runtime-test

`tools/smoke.sh` wraps everything below; prefer it. Needs the ROM (`../run/*.z64` or
`D64_ROM`); prints PASS/FAIL/SKIPPED (exit 5 = no ROM/display, report it as skipped).

Common runs:
- Title only: `tools/smoke.sh --frames 400`
- In MAP01: `tools/smoke.sh --newgame --frames 120 --shots 30,90` (frames relative to level start)
- Ray tracing: add `--renderer rt`; Vulkan raster: `--renderer vulkan`
- Settings for one run: `--set fps 120 --set show_stats 1`
- Audio: `--wav` → `build-logs/smoke/out.wav` (deterministic, 1/30 s per frame)

Look at screenshots by converting/compositing a few PNGs into one image (PIL) and reading
that single image — do not read many full-size images.

Environment hooks (src/port/i_main_pc.c, input.c, audio_pc.c, gbi.c):
| var | effect |
|---|---|
| `D64_FIXED_TIMESTEP=1` | deterministic 30 Hz frames; in-between frames rendered as shot_NNNN_k |
| `D64_QUIT_AT=n` | exit at frame n |
| `D64_SHOTS=a,b` | save shot_NNNN.bmp |
| `D64_PRESS="f:mask[/len],..."` | pad buttons (hex high 16 bits): 1000 START, 8000 A, 4000 B, 2000 Z(fire), 0800 up, 0400 down, 0200 left, 0100 right |
| `D64_PCACT="f:mask[/len]"` | PC actions: 1 jump, 2 aim down sights, 4 kick |
| `D64_LOOK="f:deg,..."` | add mouse-look pitch at frame f |
| `D64_TRACE=1` | per-frame gametic/RNG/player position (compare runs for determinism) |
| `D64_WAVOUT=file` | write audio | 
| `D64_DUMPSAMPLES=dir`, `D64_DUMPTEX=1`, `D64_DUMPCMDS=frame` | dump sound samples / textures / draw commands |
| `D64_CRASH_AT=n` | crash on purpose (tests the crash handler) |

Timeline under fixed timestep: press at ≥300 leaves the title demo; title menu ~350;
New Game 380 + skill 440 → MAP01 ~500. The game log is
`~/.local/share/Doom64RTX/Doom64RTX/doom64rtx.log` (CRASH blocks have backtraces; symbolise
with `addr2line -f -e build/doom64rtx 0x<offset>`).
