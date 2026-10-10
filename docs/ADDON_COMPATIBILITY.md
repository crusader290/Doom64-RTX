# Native add-on compatibility

This engine keeps Doom 64's native actors, maps and save format. It does not run
ZScript, DECORATE state machines, ACS, UDMF or SBARINFO. It loads PNGs and RT
material metadata and ports selected archived behaviors to C. Unknown classes
and descriptors are reported in doom64rtx.log; a loaded archive does not imply
that every resource in it is usable.

| Archived add-on | Native behavior / current limit |
|---|---|
| d64r-blood-persist.pk3 | Persistent floor/wall stains, directional damage/explosion sprays and literal monster BloodColor properties. Level reset and FIFO cap; native cosmetic physics/RNG. |
| d64r-rt-flashlight.pk3 | Battery, five-segment HUD, toggle/burnout cues and a shadowed RT cone aimed with the camera. Native timing: 30 seconds drain, 15 seconds recharge; manual toggle, fresh battery on map/load. Original audio/readout CVars are not executed. |
| d64r-lostsoul-rt.pk3, d64r-caco-ball-recolor.pk3 | Compatible sprite replacements, consolidated into the release pack. |
| d64r-bulb-textures.wad | SFLATC and SPACECE PNGs extracted into the single pack; its WAD is not loaded as a map. |
| Retribution-RT-Materials | Normal, roughness/metallic and emissive maps/defaults supported for matching native lump names. No scene-specific UDMF geometry is imported. |
| d64r-rt-sky.pk3, d64r-rt-titlelogo.pk3 | Scripted map selection/title overlay and music are not ported. Composite skies need native layer conversion. Original native sky animation/title art remain. |
| d64r-lava-fx.pk3, d64r-poison-fx.pk3 | Custom actor/environment handlers need native ports and texture-name mappings; not executed. |
| d64r-mugshot.pk3, d64r-mugshot-DEBUG.pk3, d64r-widescreen-gfx.pk3 | SBARINFO/custom face HUD and INTERPIC layout are not ported; native proportional widescreen/HUD remains. |
| barrel-boom-test, shooter-debug, weapon-fire-probe, gallery/enemy-gallery tours | GZDoom diagnostic actors, scripted probes/tours and map changes are not executed. Native F7 tools remain available. |
| *lab/*gallery MAPINFO packs, map01-rtfix and replacement WAD maps | Require native map/special conversion. Unsupported descriptors are logged; WAD map packs are skipped explicitly. |

## Loading and controls

The release needs only packs/doom64rtx-visuals.pk3. Selected upstream definitions
are stored inside it under native/, with per-entry provenance in manifest.json.
The adapters also recognize the original blood/flashlight PK3s loaded with
`-pack path.pk3` or `packs = a.pk3;b.pk3`. Literal BloodColor values are recognized
only for mapped native monster classes; arbitrary new actors cannot be created.
No script evaluation, external commands or script-driven filesystem access occurs.

Settings > Gameplay exposes Native Add-ons, Flashlight, Blood Lifetime and Blood
Limit. Lifetime is measured in native 30 Hz tics in the ini: -1 selects pack
policy (permanent with the blood adapter, otherwise 30 seconds), 0 is permanent.
Limits are 1..128; the original add-on's unlimited/1500-actor policy is bounded.
Settings affect new stains; existing stains clear on level/load or Gore Off.
Flashlight needs the RT renderer; Vulkan raster/OpenGL cannot render its cone.
F is the default toggle and can be changed under Key Bindings. Flicker appears
near burnout. Pause freezes the battery. Demos/recording disable the ports.

CVARINFO, original burst/roll controls, general inheritance and actor state
instructions are not interpreted. Native equivalents are deliberately documented
as adaptations, rather than full GZDoom script compatibility.

## Validation

Sanitizers cover the real bounded lexer, comment/string rejection, literal colors,
limits, permanent stain expiry/caps, binding persistence and flashlight edge,
burnout/recharge/reset and demo gates. C/Rust/GPU spotlight records are 64 bytes.
GL/Vulkan/RT gameplay smoke and matching Windows RT off/on captures verify that
the cone changes world illumination. No exact replay of the original user crash
camera or full Retribution map playback is claimed.
