# Native add-on compatibility

This engine keeps Doom 64's native actors, maps and save format. It does not run
ZScript, DECORATE state machines, ACS, UDMF or SBARINFO. It loads PNGs and RT
material metadata and ports selected archived behaviors to C. Unknown classes
and descriptors are reported in doom64rtx.log; a loaded archive does not imply
that every resource in it is usable.

| Archived add-on | Native behavior / current limit |
|---|---|
| d64r-blood-persist.pk3 | Persistent floor/wall stains, directional damage/explosion sprays and literal monster BloodColor properties. Level reset and FIFO cap; native cosmetic physics/RNG. |
| d64r-rt-flashlight.pk3 | Unlimited shadowed RT cone aimed with the camera, with a manual toggle cue. Battery, recharge, burnout, flicker and flashlight HUD removed at the owner's request. Original audio/readout CVars are not executed. |
| d64r-lostsoul-rt.pk3, d64r-caco-ball-recolor.pk3 | Compatible sprite replacements, consolidated into the release pack. |
| d64r-bulb-textures.wad | SFLATC and SPACECE PNGs extracted into the single pack; its WAD is not loaded as a map. |
| Retribution-RT-Materials | Normal, roughness/metallic and emissive maps/defaults supported for matching native lump names. No scene-specific UDMF geometry is imported. |
| d64r-rt-sky.pk3, d64r-rt-titlelogo.pk3 | Seven native panoramas selected by original sky type, yaw/pitch and widescreen; original sky logic/RNG still runs. Native title-logo fade and one-shot PCM sting through Music Volume. Fire/void/evil skies retain original rendering. Composite layers, sky actors, lightning bolts and replacement-map selection are not executed. |
| d64r-lava-fx.pk3, d64r-poison-fx.pk3 | Native 48-particle liquid adapter: poison bubbles on SLIME/D64N, ballistic lava sparks on HLAVA/D64LAVA. Separate cosmetic RNG, portal visibility checks, ceiling/floor collision, interpolated depth-tested billboards and at most eight small RT lights. Native ROM has poison floors but no matching lava floors. Original saturation/burst CVars, fragment splitting and arbitrary actors are not executed. |
| d64r-mugshot.pk3 | Recognizes DrawMugShot as a native HUD profile. Unmodified portraits react to health, damage, death, invulnerability, firing and weapon pickups; bottom health/armor/ammo/key bar with attributed Brutal Doom digits/panel. General SBARINFO is not executed. |
| BrutalDoomPlatinum SMG / assault rifle | Native variants of pistol/chaingun slots: original frames, base damage 12/20, faster cadence, ADS spread reduction and firing PCM. Existing pickups, ammo ownership and saves; Imported Weapons toggles variants. No magazines, reload scripts, grenade launcher or full arsenal parity. |
| d64r-mugshot-DEBUG.pk3, d64r-widescreen-gfx.pk3 | Diagnostic HUD scripts and INTERPIC replacement layouts need conversion. Native proportional widescreen remains. |
| barrel-boom-test, shooter-debug, weapon-fire-probe, gallery/enemy-gallery tours | GZDoom diagnostic actors, scripted probes/tours and map changes are not executed. Native F7 tools remain available. |
| *lab/*gallery MAPINFO packs, map01-rtfix and replacement WAD maps | Require native map/special conversion. Unsupported descriptors are logged; WAD map packs are skipped explicitly. |

## Loading and controls

The release needs only packs/doom64rtx-visuals.pk3. Selected upstream definitions
are stored inside it under native/, with per-entry provenance in manifest.json.
The adapters also recognize original blood/flashlight/liquid/face HUD PK3s loaded with
`-pack path.pk3` or `packs = a.pk3;b.pk3`. Literal BloodColor values are recognized
only for mapped native monster classes; arbitrary new actors cannot be created.
No script evaluation, external commands or script-driven filesystem access occurs.

Settings > Gameplay exposes Native Add-ons, Flashlight, Blood Lifetime and Blood
Limit, Face HUD and Liquid Effects. Lifetime is measured in native 30 Hz tics in the ini: -1 selects pack
policy (permanent with the blood adapter, otherwise 30 seconds), 0 is permanent.
Limits are 1..128; the original add-on's unlimited/1500-actor policy is bounded.
Settings affect new stains; existing stains clear on level/load or Gore Off.
Flashlight needs the RT renderer; Vulkan raster/OpenGL cannot render its cone.
F is the default toggle and can be changed under Key Bindings. No battery or HUD
remains. Demos/recording disable gameplay ports; the native TITLEMAP may display
the title overlay. Graphics exposes Sky Upgrade and Title Intro. Audio > New
Sounds disables imported effects, weapon PCM and title sting. Imported effects
use a conservative 0.32 gain before Effects Volume; title gain is 0.5 before Music Volume.

`-warp 7 -skill 1` starts a native map directly, skipping the title sequence.
Maps are 1..32, skills 1..5. These choices are never saved in the ini.
For fixed-timestep headless regression tests only, D64_TEST_POSITION=x,y,degrees
sets the level-start player position; D64_ENV_TRACE reports liquid sector centers.
D64_TEST_WEAPON selects a native weapon enum for fixed-timestep headless fixtures
only (2 = SMG/pistol, 5 = rifle/chaingun). Normal level entry resets look pitch and
recoil to face straight ahead; loading a saved game restores its saved view.

CVARINFO, original burst/roll controls, general inheritance and actor state
instructions are not interpreted. Native equivalents are deliberately documented
as adaptations, rather than full GZDoom script compatibility.

## Validation

Sanitizers cover the real bounded lexer, comment/string rejection, literal colors,
limits, permanent stain expiry/caps, binding persistence and unlimited flashlight
edges/reset/demo gates. Native weapon ammo/damage/ADS/render purity and actual
jump takeoff/landing paths are sanitizer-tested. C/Rust/GPU spotlight records are 64 bytes.
GL/Vulkan/RT gameplay smoke and matching Windows RT off/on captures verify that
the cone changes world illumination. No exact replay of the original user crash
camera or full Retribution map playback is claimed.
