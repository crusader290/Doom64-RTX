# Doom 64: Retribution add-ons from doom64-rt

Copied (pk3/wad files and `Retribution-RT-Materials/`) from
https://github.com/jlrouzies-fr/doom64-rt/tree/main/Doom64-Retribution
at commit `750c1d84f87546de38fe8ad774da59ff6a606ed0` at the repository owner's request.

**Licence status:** the doom64-rt repository ships no licence file, and its CREDITS.md notes
that some of these WADs are derived from **Nevander**'s *Doom 64: Retribution* maps
(`d64r-seqlight-fix.wad`, `d64r-smonf-lights.wad`, the `*-rtfix` / `*-sky3dfix` WADs) and that
permission was being sought. Credit: **jlrouzies** (doom64-rt) and **Nevander** (Doom 64:
Retribution). These files are not covered by this repository's GPLv3 licence.

## What works in Doom64-RTX

These are mods for GZDoom-RT running *Doom 64: Retribution*. Doom64-RTX runs the original
N64 game code, so only the **art** in them can be used:

| Content | Used? |
|---|---|
| `sprites/*.png`, `textures/*.png`, `flats/*.png`, `graphics/*.png` whose names match Doom 64 lumps | yes, as replacements (see README "Resource packs") |
| ZSCRIPT, DECORATE, MAPINFO, GLDEFS, UDMF maps (TEXTMAP), ACS | no (GZDoom only), listed in doom64rtx.log |
| `Retribution-RT-Materials/rt/mat/*_{orm,n,e}.png` and `rt/data/*.json` (RTGL1 conventions: G = roughness, B = metallic in `_orm`; light colours) | yes, by the ray tracer (`_h` and the `_dev`/quarantine copies are ignored) |

From source they are not loaded automatically: use `-pack <file or folder>` or copy them into
a `packs/` folder next to the executable (auto-loaded). Release builds bundle
`d64r-lostsoul-rt.pk3`, `d64r-caco-ball-recolor.pk3` and the materials (as
`doom64rt-materials.pk3`) in `packs/`; the other packs here only contain GZDoom scripts/maps or
art that does not match Doom 64 lump names.
