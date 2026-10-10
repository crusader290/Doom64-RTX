# PROJECT_CONTEXT — Doom64-RTX

Read PORT_MANIFEST.md, docs/tasks.json, docs/DISCOVERIES.md, docs/DEVLOG.md and
docs/ADDON_COMPATIBILITY.md. History lives in DEVLOG; this file is current state.

## Branch and publishing
Separate branch codex/immersive-gore-crash-fix from Claude 4ef9999. User authorized
regular pushes, builds and milestone releases; no PR requested. Never commit ROMs.
Published GitHub releases: 0.5.0 crash/gore/menu/pack/build launchers; 0.5.1 combat
animation/audio and weapon/save bugs. 0.5.2 native ports/key bindings in final QA.

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
Publish tested 0.5.2 archives/checksums with exact binary hash, push manifests.
Remaining add-on behaviors include environmental effects, composite sky/title and
face HUD. Do not claim every archived script/map is compatible. Keep attribution;
Retribution source assets have no upstream file licence, owner requested inclusion.
BDP declares GPLv3 but DetailedCredits does not identify every asset author.
