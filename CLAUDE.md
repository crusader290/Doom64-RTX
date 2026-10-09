# Doom64-RTX — instructions for Claude

Native PC port of Doom 64 (DOOM64-RE) with an SDL3 platform layer and a Rust Vulkan/RT/GL
renderer. Start every task by reading `PROJECT_CONTEXT.md` (short) and the relevant entry
in `docs/tasks.json`. Look things up in `PORT_MANIFEST.md` (file index) and
`docs/DISCOVERIES.md` (verified facts, invariants, formats) before re-investigating.

## Hard rules
- Never commit ROMs or extracted game data (`*.z64`, `DOOM64.WAD`, ...). Archived add-ons
  under `addons/` are the only committed WADs.
- Work on branch `claude/amazing-archimedes-7wr3t5`; commit + push after each verified step;
  no PR unless asked; never rewrite pushed history.
- Game sources (`src/doom64/`): keep changes minimal, mark them `[PC]` and guard with
  `#ifdef D64_PC`. Platform code goes in `src/port/` behind `I_PC*` functions.
- `d64gfx.h` and `renderer/src/ffi.rs` must stay in sync (bump `D64GFX_API_VERSION`).
- Demos and `D64_TRACE` must stay deterministic: PC-only gameplay changes are disabled
  while `demoplayback || demorecording`.
- Do not claim something works without evidence (build log, smoke run, screenshot, trace).

## Commands
- Build/validate: `tools/check.sh [linux|win|null|all]` — incremental; prints only the
  first errors and a failure class; full log in `build-logs/<target>.log`.
- Runtime test (needs the ROM, headless): `tools/smoke.sh [--newgame] [--shots 30,60]
  [--press f:mask,...] [--renderer gl|vulkan|rt] [--wav] [--set key value]`.
- Environment: `tools/setup_env.sh --check` (fast) / `tools/setup_env.sh` (install).
- Release: `tools/make_release.sh <version>` after `tools/check.sh linux win`.

## Working method (saves tokens and retries)
1. Read state → inspect only the needed code (`grep -n`, `sed -n a,bp`; avoid whole files
   and directory dumps) → smallest safe change → `tools/check.sh` → smoke test if runtime
   behaviour changed → commit → update `docs/tasks.json` / `PROJECT_CONTEXT.md`.
2. On a failure, fix the first error only; downstream errors are usually cascades.
   `tools/check.sh` refuses to rebuild identical failing sources (exit 4) and warns after 3
   attempts at the same error: change the hypothesis, read more context, or stop and record
   the blocker in `docs/tasks.json` (failure class + evidence + next step).
3. Environment failures (exit 3 / class `env`) are fixed with `tools/setup_env.sh`, never
   by editing code.
4. Keep reports short: what changed, evidence, next step.

Skills: `build-diagnose`, `runtime-test`, `session-handoff` (in `.claude/skills/`).
