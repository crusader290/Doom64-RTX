# PROJECT_CONTEXT — Doom64-RTX (current state)

Read this first; it is kept short on purpose. Durable facts: `docs/DISCOVERIES.md`.
Per-file status / index: `PORT_MANIFEST.md`. Task ledger: `docs/tasks.json`.
History: `docs/DEVLOG.md`. Update this file at milestones and before ending a session
(replace, do not append).

## Goal (from the user)
Native PC port of Doom 64 from DOOM64-RE (GPLv3): SDL3 platform layer, Rust renderer with
Vulkan 1.1+ (ash) + toggleable ray tracing (ideas from jlrouzies-fr/doom64-rt, no shared
code) + OpenGL 3.3 fallback (glow); x86-64 Windows and Linux; ROM auto-detected in the
working directory (never commit ROMs). Modern-play extras in the spirit of Brutal Doom
(ideas only, no code from GZDoom mods). Develop on branch `claude/amazing-archimedes-7wr3t5`;
no PR unless asked. Releases go to `releases/` (`tools/make_release.sh <ver>`).

## Milestone
Playable port with sound, RT preview with doom64-rt materials, resource packs, modern
controls, 60/120 fps, save/load, QoL extras. Release 0.3.0.

## Verified working (evidence in docs/tasks.json)
GL + Vulkan raster renderers, RT on lavapipe, ROM detection, sound/music, 16:9, logging +
crash reports, options pages, mouse look/jump, interpolation (logic identical at 30/120 fps),
save/load (pause menu, title, F5/F9), ADS zoom, resource packs (pk3 + folders, textures and
sprites), RT materials (emissive/occlusion visible on lavapipe), Windows build under Wine.

## Implemented, not fully verified
F7 debug page (no key injection in tests), weapon keys 1-8, kick damage/push, extra gore,
fast weapons, autosave slot.

## Build / test status
- `tools/check.sh linux win` — OK at the 0.3.0 release commit.
- `tools/smoke.sh` — PASS (GL, Vulkan, RT, with and without packs).
- Last known-good release: 0.3.0 (`releases/`), both archives smoke-tested from fresh
  extractions with a pk3 in `packs/` (Linux native, Windows under Wine).

## Known problems
- MSVC build unsupported (needs zero-init of uninitialised locals) (T16).
- Saves are tied to struct sizes; a build that changes `mobj_t`/`player_t`/`sector_t`/
  `line_t` refuses older saves.

## Environment notes
- Prebuilt SDL3 in `../deps/sdl3-linux` (made by `tools/setup_env.sh`); without it CMake
  fetches and builds SDL3 (slow). ROM for tests: `../run/*.z64` (or `D64_ROM`).
- Xvfb does not survive container restarts; `tools/smoke.sh` starts it.

## Next recommended task
T17 RT emitter lights from emissive textures (doom64-rt lightColorHEX) — accept when a lit
fixture visibly lights nearby walls in `tools/smoke.sh --renderer rt --newgame` with
`-set packs addons/doom64-retribution/Retribution-RT-Materials`.
