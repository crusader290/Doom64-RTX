# PROJECT_CONTEXT — Doom64-RTX

Read PORT_MANIFEST.md, docs/tasks.json, docs/DISCOVERIES.md, docs/DEVLOG.md and
docs/ADDON_COMPATIBILITY.md. History lives in DEVLOG; this file is current state.

## Branch and publishing
Separate branch codex/immersive-gore-crash-fix from Claude 4ef9999. User authorized
regular pushes, builds and milestone releases; no PR requested. Never commit ROMs.
Published GitHub releases: 0.5.0 crash/gore/menu/pack/build launchers; 0.5.1 combat
animation/audio and weapon/save bugs; 0.5.2 native blood/flashlight ports/key bindings.
0.5.3 native portrait HUD/liquid effects is being packaged after validation.

## Implemented behavior
0.4.0 fault RVA 0x7ebc was FixedDiv2, caller 0x2736c was R_CheckBBox. Fixed divide
boundaries, projection depth and INT_MIN/shift hazards. Windows logs fault
registers, module-relative frames and cause; exact user camera unavailable.

Aggressive defaults: 120 FPS, always run, faster switch/attack recovery and stronger
kick. Native render-only recoil/inertia/breathing, hit feedback and attributed
13-frame BDP boot animation. Kick lands 3 tics after start. Four attributed PCM
clips (pistol, melee impact, slop, kick), fixed 16-voice mixer, pan/volume/reverb.
Modern combat and cosmetic effects are gated off during demos/recording.

Gore has 128 particles/gibs and 128 floor/wall stains, collision/gravity, own RNG,
no game actors or save struct changes. Persistent-blood adapter adds permanent
stains and literal monster colors; lifetime/cap configurable. Flashlight adapter
adds battery/HUD/cues and a shadowed RT cone; toggle F, 30s drain/15s recharge,
manual switch, charge resets on map/load. C/Rust/GPU lights all have 64-byte stride.

User selected native behavior ports, not another engine. No general ZScript,
DECORATE states, ACS, UDMF or SBARINFO runtime. Loader recognizes selected upstream
handlers and mapped BloodColor literals, logs unsupported descriptors/classes.
Capability matrix records remaining unported production/test add-ons. One PK3
contains selected declarations, compatible sprites/ceiling art/RT materials,
boot frames/PCM and per-file credits. Most material PNGs are not new albedo.

Menus use Share Tech Mono (OFL), scalable atlas and ordered overlay geometry.
Settings: Graphics, Gameplay, Audio, Key Bindings, Controls. 12 primary key actions,
conflict swap, cancel/reset/persistence; core menu/weapon/function shortcuts fixed.
Confirmation pages match; console password/storage/controller screens bypassed.
Native slots/F5/F9/autosave stay. Macro globals reset at level start; load repairs
macro line vertices. Backward weapon scan uses signed index to avoid underflow.

## Validation
Linux, cross MinGW and downloaded native Windows builds. Sanitizer math (396608
BSP cases), gore, PCM, animation, native lexer/battery/ABI and binding persistence
checks. 1050-frame animation/audio on/off combat traces identical. Actual MAP01
slot save/load passed, backward weapon crash replay passed 1400 frames. GL/Vulkan/
RT smoke, native Windows RT combat and flashlight off/on illumination captures.
0.5.1 fresh Windows default RT/audio passed; Linux software RT needed 320x240/30fps
for timeout (default full-resolution run was slow, not a crash).

## Build environment
Container doom64-codex-build binds this repo at /repo. D64_DEPS=/repo/build-deps;
SDL prefix /repo/build-deps/sdl3-linux; source ~/.cargo/env. ROM /tmp/test-rom.z64
is external/user-owned. Native Windows portable MSYS under build-deps/msys64;
MSYSTEM=MINGW64 and D64_BUILD_ROOT=absolute repo, invoke tools/build_native_windows.sh
with its absolute /c/... path in login bash. build.bat handles this setup/download.
Native SDL also needs libiconv-2.dll; launcher copies it. Release Windows uses
build-win. Root build.sh builds Linux/cross Windows. Native SDL/GCC versions differ
from the pinned cross SDL. Logs/captures/build trees ignored; do not stage ROMs.

## Next work
Publish tested 0.5.3 archives/checksums with exact binary hash, push manifests.
Remaining production add-on visual behaviors include composite sky/title.
Native portrait HUD and bounded poison/lava particles are implemented/validated. Do not claim every archived script/map is compatible. Keep attribution;
Retribution source assets have no upstream file licence, owner requested inclusion.
BDP declares GPLv3 but DetailedCredits does not identify every asset author.

0.5.3: native DrawMugShot profile uses 42 unmodified portrait PNGs, reactive damage,
death/god/fire/pickup states and clean stat/key/battery layout. Liquid FX use a
48-particle pool and separate RNG, portal trace, native texture mappings, depth
billboards and up to eight small RT lights. Native maps lack matching lava flats.
Linux GL/MAP07 and Windows hardware RT on/off captures show effects; all seven
sanitizer suites pass. Transient -warp/-skill startup and headless fixture position
added. No new actors or save fields. 550-frame gameplay/RNG on/off trace matches.

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
