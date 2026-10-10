# DEVLOG — Doom64-RTX history

Narrative session history, moved verbatim out of PROJECT_CONTEXT.md (which now holds only
the current state). Append new entries at the bottom only for milestones; durable facts go
to docs/DISCOVERIES.md, task state to docs/tasks.json.

## Session log (moved from PROJECT_CONTEXT.md)
### 2026-10-08 — session 1 (restarted several times; earlier attempts left nothing pushed)
- Lesson: **commit + push early and often**; previous workers were restarted and lost all work.
- Imported DOOM64-RE sources (commit 6931e678a0b2958be1b49598f2fe60712c6596e1) into src/doom64.
- Reference doom64-rt at 750c1d84f87546de38fe8ad774da59ff6a606ed0.
- Bugs found & fixed while bringing it up (watch for the same classes elsewhere):
  - `W_CheckNumForName` masks name words with `0x7fffffff` (big-endian) -> byte-swap masks.
  - `P_GroupLines` allocated `total*4` for an array of pointers -> heap corruption on 64-bit.
  - Header-defined globals (`R_RenderSKY`, `gametic`) need `extern` with modern GCC (-fno-common).
  - `boolean` enum clashed with stdbool's `true/false` macros pulled in by SDL -> `typedef int`.
  - LoadTLUT must read from the start of the texture image (not the load tile's stale `uls`);
    wrong palettes looked like noise and also defeated the texture cache.
  - Asset headers (`spriteN64_t`, `textureN64_t`, `gfxN64_t`) are big-endian -> `BE16()` on reads;
    demo lumps are big-endian ints -> swapped after `W_ReadLump`; passwords keep N64 byte order.
- Milestone: OpenGL fallback renders the legal screen and the title-map demo correctly
  (docs/img/gl_title_demo.png).
- Milestone: Vulkan raster backend (ash) works on lavapipe; with `D64_FIXED_TIMESTEP=1` its
  frame 100 is pixel-identical to the GL backend.
- FIXED: game logic was not deterministic between runs. Cause: the reconstructed code reads
  uninitialised locals (gcc -Wmaybe-uninitialized lists ~25, e.g. p_enemy.c, p_pspr.c,
  r_phase3.c); stack garbage differed per run (ASLR) and per renderer, changing how often
  P_Random was called. Fix: build game code with `-ftrivial-auto-var-init=zero`.
  Verified with `D64_TRACE=1` (per-frame gametic/P_Random/camera trace): GL, GL, Vulkan
  identical over 600 frames. MSVC has no equivalent flag -> prefer GCC/Clang/MinGW, or fix
  the individual locals (TODO).
- Milestone: Vulkan ray traced world (renderer/src/vk/rt.rs + shaders vk_rt.comp,
  vk_denoise.comp, vk_composite.frag) runs on lavapipe and composites correctly.
- User report "a lot of textures don't look loaded" (2026-10-08). Findings:
  - Texture decoding verified correct by dumping (D64_DUMPTEX): walls, flats, sprites, weapon.
  - BUG: weapon sprite drew black. Texture rectangles had shade alpha 255, and the psprite
    combiner runs with the fog blender (FOG_SHADE_A) -> 100% fog (black). The RDP has no shade
    for rectangles; now rgb=white, alpha=0.
  - The game's brightness defaults to 0 (very dark, as on N64), which makes surfaces look
    untextured. New ini setting `brightness` (default 50) seeds the in-game Brightness option.
  - The white full-screen flash after pressing Start on the title is the game's own effect
    (prim LOD fraction = sector light level in COMB07), not a renderer bug.
  - D64_DUMPCMDS=<frame> prints a frame's resolved draw commands (gbi.c).
- Windows: MinGW-w64 cross build works (only system DLLs + SDL3.dll); exe verified under Wine 9
  with OpenGL and Vulkan (lavapipe through winevulkan, RT available).
- Release 0.1.0 preview committed to releases/ (Linux tar.gz with bundled libSDL3 and
  `$ORIGIN` runpath set via patchelf; Windows zip with SDL3.dll; both include doom64rtx.ini,
  README, LICENSE, RELEASE_NOTES.txt). Both smoke-tested from fresh extractions.
  To rebuild a release: build `build` (Linux Release) and `build-win` (MinGW), strip, copy
  SDL libs, `patchelf --set-rpath '$ORIGIN'`, archive as releases/doom64rtx-<ver>-<os>-x86_64.*
- Widescreen (user request): Options > Display > Aspect Ratio. gbi.c keeps 2D in a centred
  4:3 area of a virtual 426.7x240 screen (NDC x scaled by 320/vw), stretches full-width 2D
  (sky, fades, clears, wipes) and SKY triangles, widens 3D (clip x scaled; RT proj too);
  r_phase1.c scales lateral view coords by R_PCFovInvScale so BSP culling covers the wider
  view. ABI v2: D64GfxFrame.virtual_width (viewport aspect + scissor units).
- Logging (user request): src/port/log.c, on by default; crash handler with backtrace.
- RT in game: works (lights from rtlights.c). KNOWN ISSUE: translucent world surfaces (e.g.
  MAP01 doorway grate, BLEND+ALPHA_THRESH mid-texture) mostly disappear in RT mode even with
  the depth test disabled -> investigate composite ordering / alpha.
- Release 0.1.1 in releases/ via tools/make_release.sh.
- NEXT (user): crash hunting (ASan/UBSan soak runs, long automated play), then WESS audio.

### Session 2 — audio (WESS) and gameplay modernisation
- Sound works: WESS + src/port/n64synth.c (clean-room alSyn*). Music and SFX verified by
  rendering to WAV (`D64_WAVOUT=file.wav`, deterministic with `D64_FIXED_TIMESTEP=1`,
  1/30 s per frame) and looking at spectrograms; Windows (Wine) output RMS identical.
  `D64_DUMPSAMPLES=<dir>` writes all 124 bank samples (VADPCM decode checked visually).
- Data formats (all big-endian): WMD = module_header(32) + patch_group_header(24) + patch
  bank: patches(4 B each) | patchmaps(20) | patchinfo(24: base, len, type, flags, pad,
  pitch(!), loopindex, unused) | drummaps(4) | loopinfo(8) | raw loops(16) | ADPCM loops(48,
  state[16]) | ADPCM books(264, one per sample index). Sections 8-byte aligned relative to
  the bank start. WSD = module_header + table of 16-byte records (seq_header 12 + pointer)
  then per-sequence track blocks (track_header 20, labels u32[], event bytes; event
  parameters are little-endian byte pairs). WDD = raw sample data (offsets from patchinfo).
- WESS needs **unsigned char** (`*lpdest != 0xFF` track tests) -> `-funsigned-char` on the
  audio sources only; with signed char it crashed in queue_wess_seq_stopall on map change.
- Threading: synth + sequencer run in the SDL audio callback; s_sound.c wrappers lock the
  same mutex, wess_disable/enable are no-ops (wesssys_disable_ints).
- Not emulated exactly: N64 exponential envelope ramps (linear here), 22050 Hz output
  (default audio_rate=44100, pitch ratios scale automatically), reverb params approximated.
- Container setup used: `apt-get install glslang-tools mingw-w64 wine64 wine patchelf libvulkan-dev libx11-dev
  libxext-dev libwayland-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev`.

### 2026-10-10 — Codex 0.5.0 milestone
- Separate branch codex/immersive-gore-crash-fix; c08e42a crash fix already pushed.
- Bounded cosmetic gore with demo-safe RNG and collision; stronger kick/fast combat defaults, 120 FPS.
- Retro TrueType UI revised after user feedback; removed PC password/console storage/controller menus.
- Consolidated compatible Retribution art into one reproducible PK3 with per-file provenance.
- Added root Linux/Windows build launchers with dependency downloads; native setup verified.
- Sanitizer tests, both platform builds and GL/Vulkan/RT smoke passed; final packaging/native build pending.
- Remaining requested milestones: animations/sounds, broader QoL, add-on script compatibility.
- Final verification: native build.bat compilation PASS; fresh Windows RX 7900 XTX and Linux Mesa RT/audio/MAP01 release tests PASS; forced Windows 0xc0000094 diagnostic report PASS. Archives and SHA256SUMS ready for publication.

## 2026-10-10 — combat animation/audio milestone in validation
Native recoil/inertia/breathing, attributed BDP kick frames, hit marker and delayed
kick impact; four attributed PCM overrides with fixed voice/tail limits. Audio
and animation sanitizer checks pass; 1050-frame GL traces match with both effects
on/off. Native Windows RT ran 1000 frames normally. Its downloaded SDL dependency
now also bundles libiconv. Save/load navigation exposed a separate unsigned-enum
underflow in backward weapon selection; fixed signed loop index, replay pending.
Level-start macro globals reset and loaded macro line vertices repaired without
changing save structs. User selected native add-on behavior ports, not a new engine.
Validation complete: actual MAP01 slot 0 save/load passed (117 actors, 38 thinkers); backward weapon crash replay ran 1400 frames normally. 0.5.1 binaries being packaged.

## 2026-10-10 — 0.5.1 published; native ports in validation
0.5.1 is published with both archives/checksums. Fresh Windows default RT/audio passed; fresh Linux RT/audio passed at 320x240/30fps because full-resolution software Vulkan exceeded the timeout. Native adapters now recognize selected blood/flashlight declarations and literal BloodColor properties. Persistent stains remain hard-capped at 128. A native battery/HUD and actual cone-light shader are implemented; C, Rust FFI and GPU packing all use a 64-byte light record. Unknown script classes are logged. No general VM or map compatibility is claimed.

## 2026-10-10 — 0.5.2 final validation
Windows RT cone off/on captures show increased world illumination; fixed intermediate GPU record packing to match the 64-byte shader record. Added 12-key rebinding, conflict swaps, persistence/cancel/reset, consistent confirmation pages, native capability matrix and alphabetical pack discovery. Adapter, battery, permanent-stain, binding and configuration sanitizer checks pass. Remaining production add-on behaviors are tracked explicitly; no general VM claim.

## 2026-10-10 — 0.5.3 native face HUD and liquid effects
0.5.2 archives/checksums published. Native DrawMugShot profile, unmodified reactive
portraits and HP/armor/ammo/key/battery layout added. Poison/lava ports use bounded
cosmetic particles, own RNG, portal visibility, ceiling/floor collision, growing
frame geometry, interpolated depth-tested billboards and up to eight small RT
lights. Native ROM has slime floors but no matching lava flats. Original burst,
fragment and saturation CVars are not interpreted. One PK3 now holds the art and
selected descriptors. Face HUD/Liquid Effects can be toggled independently.
Transient -warp/-skill starts native maps directly; headless-only fixture position
and liquid-center diagnostics support regression captures. Seven sanitizer suites
PASS; MAP01/MAP07 GL, all 32 native-map startups, 550-frame on/off gameplay/RNG
trace equality and Windows RT on/off captures PASS. Windows RT changed 2557 pixels
with stronger green channel near bubbles; no full script/map VM claim.

## 2026-10-10 — 0.5.4 UI, native arsenal and sky/title milestone
0.5.3 was published with both archives/checksums. 0.5.4 centers main/pause items
with original lettering/skull cursor and simplifies settings styling. Bottom HUD
uses attributed Brutal Doom panel/digits, centered reactive face and native stats.
Flashlight battery/HUD and hitmarkers removed; unlimited beam keeps manual toggle.
Imported effects gain reduced from 0.65 to 0.32; New Sounds toggles effects,
weapon PCM and title sting (0.5 music gain), with ROM fallback. Native SMG/rifle
variants use pistol/chaingun slots and existing ownership/ammo/save fields; damage
12/20, faster cadence, narrowed ADS spread, original PNGs and firing PCM. No
magazine/reload, launcher or complete arsenal/actor VM is claimed.
New level pitch/recoil reset; saved views restored on load. Actual jump regression
verifies takeoff-only grunt, quiet normal/long jump landings and retained ordinary
fall grunt. Seven panoramas map native sky type/yaw/pitch/widescreen; original sky
simulation/RNG preserved. Native title fade/one-shot music, fallback when disabled.
Sanitizers cover actual weapon/ammo/ADS/draw purity, jump paths, unlimited beam,
PCM/combat/title/sky; Linux, MinGW cross and native Windows builds pass. Real GL
main/pause/SMG/rifle captures and Windows RT/audio smoke pass. Quit UI reaches
normal exit (smoke wrapper expects the timed exit, so its FAIL here is expected).
0.5.4 release packaging/fresh-extraction verification in progress.
