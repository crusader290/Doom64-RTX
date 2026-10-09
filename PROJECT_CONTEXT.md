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
Playable port with sound, RT preview, modern controls, 60/120 fps, save/load, QoL extras.
Next milestone: resource packs (pk3) + doom64-rt RT materials (T12, T13), RT translucency fix (T05).

## Verified working (evidence in docs/tasks.json)
GL + Vulkan raster renderers, RT on lavapipe, ROM detection, sound/music, 16:9, logging +
crash reports, options pages, mouse look/jump, interpolation (logic identical at 30/120 fps),
save/load (pause menu, title, F5/F9), ADS zoom, Windows build under Wine (0.1.1; audio RMS
identical).

## Implemented, not fully verified
F7 debug page (no key injection in tests), weapon keys 1-8, kick damage/push, extra gore,
fast weapons, autosave slot.

## Build / test status
- `tools/check.sh linux win null` — OK at commit c4f74fc + release commit (0.2.0).
- `tools/smoke.sh` — PASS (title 400 frames, GL); `--newgame` PASS.
- Last known-good release: 0.2.0 (`releases/`), both archives smoke-tested from fresh
  extractions (Linux native, Windows under Wine).

## Known problems
- RT: translucent mid-textures (MAP01 doorway grate) mostly vanish (T05).
- MSVC build unsupported (needs zero-init of uninitialised locals) (T16).
- Saves are tied to struct sizes; a build that changes `mobj_t`/`player_t`/`sector_t`/
  `line_t` refuses older saves.

## Environment notes
- Prebuilt SDL3 in `../deps/sdl3-linux` (made by `tools/setup_env.sh`); without it CMake
  fetches and builds SDL3 (slow). ROM for tests: `../run/*.z64` (or `D64_ROM`).
- Xvfb does not survive container restarts; `tools/smoke.sh` starts it.

## Next recommended task
T12 resource packs — accept when a test pk3 in `packs/` replacing one wall texture shows in a
`tools/smoke.sh --newgame --shots 30` screenshot, and `tools/check.sh linux win` passes.
