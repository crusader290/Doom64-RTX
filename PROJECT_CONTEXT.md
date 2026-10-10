# PROJECT_CONTEXT — Doom64-RTX

Read with `PORT_MANIFEST.md`, `docs/tasks.json`, `docs/DISCOVERIES.md` and `docs/DEVLOG.md`.

## Goal and branch
Native PC Doom 64 port: SDL3, Rust Vulkan/RT and OpenGL, Windows/Linux x64.
User work continues on `codex/immersive-gore-crash-fix`, branched from Claude's
`claude/amazing-archimedes-7wr3t5` at 4ef9999. No PR requested. Never commit ROMs.
User requested regular pushes, manifest updates and releases at significant milestones.

## Current milestone: 0.5.0
MAP01 divide crash fixed: 0.4.0 RVA 0x7ebc resolves to FixedDiv2, caller 0x2736c
to R_CheckBBox. Guard zero/negative projection depth and zero denominators;
Windows reports fault registers and module-relative offsets. Exact user camera
state is unavailable, so the original crash has not been replayed.

New bounded cosmetic gore: 128 particles/gibs + 128 floor/wall stains, collision,
gravity, fade, independent RNG; no extra actors/save struct changes. Disabled
during demos and recording. Defaults: 120 FPS, always run, fast switching and
25% shorter positive weapon states. Kick has more damage/knockback and shorter
cooldown. Modern extras are gated off for demos/recording.

Retro UI uses Share Tech Mono (SIL OFL), baked TrueType atlas and ordered overlay
geometry. Original title art resized; settings pages have compact rows, help,
selection indicators and pagination. Audio volume controls added. Password,
Controller Pak, Control Pad/Stick and console display menus removed from PC
flows; native save/load slots remain. Missing font falls back to original art.

One `packs/doom64rtx-visuals.pk3` replaces three separate release packs; combines
lost-soul/fireball sprites, SFLATC/SPACECE ceiling lights, RT maps and defaults.
Per-file sources recorded; exclude broken/development/quarantine content and
GZDoom scripts/maps. Most source images are material maps, not higher-res albedo.
Existing Retribution assets have no upstream licence; retain owner-requested
inclusion and attribution; do not relabel them GPL.

Root `build.sh` downloads dependencies and builds Linux/MinGW Windows;
`build.bat` downloads a SHA256-verified portable MSYS2 toolchain and builds native
Windows. Downloads and outputs live under build-deps/build-*.

## Evidence
Linux and MinGW builds pass. ASan/UBSan fixed math + 396608 BSP cases pass;
gore pool/collision/expiry/draw-purity/demo/reset tests pass. GL 2300-frame MAP01
combat passes. Gore on/off 1400-frame gameplay traces match. Vulkan 680-frame
and RT 620-frame smoke pass. Menu screenshots visually reviewed; initial large
VT323 layout replaced following user feedback. Consolidated archive passes ZIP
integrity and byte-for-byte reproducibility. Native MSYS2 dependency setup passes;
native compilation and fresh Linux/Windows release-extraction tests pass. Windows
Vulkan RT/audio/MAP01 verified on RX 7900 XTX; Linux on Mesa. Forced Windows
0xc0000094 report includes fault/module offsets, registers and division reason.

## Remaining requested work
Milestone 0.5.0 archives and checksums verified; GitHub release publication next.
Then implement smoother recoil/sway, visible kick and hit feedback; inspect/import
Brutal Doom sounds with source/licence attribution (selected source files are only
in work/sound-reference so far, not shipped). User also requested GZDoom script
compatibility for archived add-ons. General ZScript/DECORATE/ACS/UDMF support is
not implemented; inspect each script and implement supported behaviors with
explicit compatibility reporting. Do not claim all add-ons work.
Further QoL/texture/mechanics improvements remain to be reviewed and tested.

## Environment and known limitations
Build/test Docker container doom64-codex-build, /repo bind mount. SDL prefix
/repo/build-deps/sdl3-linux; source ~/.cargo/env. Test ROM /tmp/test-rom.z64,
copied outside Git. tools/smoke.sh starts Xvfb. User GPU crash-location validation
still needed. MSVC remains experimental; MinGW supported. Saves tied to struct
sizes; these changes do not change those structs. RT emitter lights still need
isolated visual validation (T17).

## 2026-10-10 — combat animation/audio milestone in validation
Native recoil/inertia/breathing, attributed BDP kick frames, hit marker and delayed
kick impact; four attributed PCM overrides with fixed voice/tail limits. Audio
and animation sanitizer checks pass; 1050-frame GL traces match with both effects
on/off. Native Windows RT ran 1000 frames normally. Its downloaded SDL dependency
now also bundles libiconv. Save/load navigation exposed a separate unsigned-enum
underflow in backward weapon selection; fixed signed loop index, replay pending.
Level-start macro globals reset and loaded macro line vertices repaired without
changing save structs. User selected native add-on behavior ports, not a new engine.
## 2026-10-10 — 0.5.1 published; native ports in validation
0.5.1 is published with both archives/checksums. Fresh Windows default RT/audio passed; fresh Linux RT/audio passed at 320x240/30fps because full-resolution software Vulkan exceeded the timeout. Native adapters now recognize selected blood/flashlight declarations and literal BloodColor properties. Persistent stains remain hard-capped at 128. A native battery/HUD and actual cone-light shader are implemented; C, Rust FFI and GPU packing all use a 64-byte light record. Unknown script classes are logged. No general VM or map compatibility is claimed.
