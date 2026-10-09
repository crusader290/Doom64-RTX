# DISCOVERIES — verified facts that are expensive to rediscover

Check here (and `git log`) before reverse-engineering an interface again. Each item was
verified in this repository; add new ones when a finding cost real investigation.
History/narrative lives in docs/DEVLOG.md.

## Engine and data facts (moved from PROJECT_CONTEXT.md)
- **DOOM64-RE is N64-native**: it renders by building F3DEX display lists (`gSP*`/`gDP*`
  macros into `GFX1`), vertices (`VTX1`) and fixed-point matrices (`MTX1`) that the RSP reads
  *later*. The vertex data is often written **after** `gSPVertex` is emitted, so the display
  list can only be interpreted once the frame is complete (`I_DrawFrame`). Our interpreter
  runs there.
- We **do not** use Nintendo's SDK headers (`ultra64.h`, `gbi.h`). `src/port/ultra64.h` is a
  clean-room shim that defines the types and the GBI macros in our own encoding:
  `Gfx = { u32 w0; uintptr_t w1; }` so pointers survive on 64-bit.
- **64-bit hazards** in the RE code: `(int)ptr` arithmetic (e.g. `(int)GFX1 - (int)GFX2` in
  `I_CheckGFX`), `(u32)` casts of ROM symbols, `long` assumed 32-bit (LLP64 vs LP64!).
  Fix pattern: pointer differences / `intptr_t`.
- **Endianness**: N64 is big-endian. The WAD *directory and map lumps* are little-endian
  (the RE uses `LittleShort`/`LongSwap` to read them on N64) → on PC those become identity.
  Texture/palette data are fed raw to the RDP, which reads memory big-endian → the GBI
  interpreter reads texel memory as big-endian bytes. CPU-written words that the RDP reads
  (e.g. `*(int*)VTX1[i].v.cn = color`) are native-endian on PC; the interpreter decodes `cn`
  accordingly.
- **Matrices**: game writes N64 `Mtx` directly (s15.16 split: words 0–7 integer halves, 8–15
  fraction halves). The projection is `R_ProjectionMatrix` loaded in `p_tick.c`.
- **ROM data offsets** (from tools/dm64ex.c), index = region:
  | region | detect | WAD off/size | WMD | WSD | WDD |
  |---|---|---|---|---|---|
  | 0 USA     | hdr[0x3E]='E', hdr[0x10]=0xA8 | 0x63D10 / 0x5D18B0 | 0x6355C0 / 0xB9E0 | 0x640FA0 / 0x142F8 | 0x6552A0 / 0x1716C4 |
  | 1 EUR     | hdr[0x3E]='P'                 | 0x63F60 / 0x5D6CDC | 0x63AC40          | 0x646620          | 0x65A920 |
  | 2 JAP     | hdr[0x3E]='J'                 | 0x64580 / 0x5D8478 | 0x63CA00          | 0x6483E0          | 0x65C6E0 |
  | 3 USA r1  | hdr[0x3E]='E', hdr[0x10]=0x42 | 0x63DC0 / 0x5D301C | 0x636DE0          | 0x6427C0          | 0x656AC0 |
  The user supplied *Doom 64 (USA) (Rev 1).z64*, sha1 `6fb0ce9c75bbe54b6e1ede337652b0221e5f2aad`
  (NOT in the repo — never commit ROMs).
- Reference RT project (doom64-rt) lessons worth keeping: painted/baked light must become real
  emitters; sector light colours drive lighting; sprites need special handling (flat normals,
  shadow proxies); temporal accumulation + denoise is mandatory at 1 spp; archived settings
  that engine code writes become "a diary" — don't persist runtime-modified settings.


## Subsystem boundaries
| Layer | Where | Rule |
|---|---|---|
| Original game logic | `src/doom64/` | DOOM64-RE code; every PC change is marked `[PC]` and guarded `#ifdef D64_PC` |
| libultra / N64 hardware shims | `src/port/include/` (ultra64.h, libaudio.h), `src/port/os.c`, `gu.c` | clean-room, no SDK headers |
| Platform (SDL3 main, input, config, ROM, log, saves, options, interpolation) | `src/port/*.c` | game-facing calls are `I_PC*` (declared in `src/port/d64pc.h`) |
| Display-list interpreter | `src/gfx/gbi.c` → `d64gfx.h` C ABI (v2) | mirror in `renderer/src/ffi.rs`; bump `D64GFX_API_VERSION` together |
| Renderers | `renderer/` (Rust): `gl_backend.rs`, `vk/mod.rs`, RT `vk/rt.rs` + `shaders/*.glsl` (SPIR-V checked in, rebuild with `tools/compile_shaders.sh`) | RT is an optional path; raster must keep working |
| Sound | WESS (`src/doom64/wess*.c`, `n64cmd.c`, `seqload*.c`) + `src/port/n64synth.c`, `audio_pc.c` | audio thread; game enters only through `s_sound.c` (locked) |

Entry points: `main` in `src/port/i_main_pc.c` → `D_DoomMain` (`src/doom64/d_main.c`);
per frame: `MiniLoop` → ticker (30 Hz) → drawer → `I_DrawFrame` → `GBI_RunFrame` →
`d64gfx_render_frame`.

## Build/runtime invariants (break these and things fail in non-obvious ways)
- Game code needs `-ftrivial-auto-var-init=zero`: the RE code reads uninitialised locals;
  without it the game is non-deterministic (verified with `D64_TRACE`).
- WESS needs `-funsigned-char` (`*lpdest != 0xFF` track tests); signed char crashed in
  `queue_wess_seq_stopall` on map change. Only the audio sources get the flag.
- `long` → `int` throughout WESS (LP64). Pointer↔int casts are `-Werror` (`uintptr_t`).
- `boolean` is `int` (enum clashed with stdbool via SDL).
- Big-endian data: asset headers (`BE16/BE32`), demos, WMD/WSD banks. WAD directory and map
  lumps are little-endian. WESS event parameters are LE byte pairs.
- Game logic runs at 30 tics/s regardless of the frame rate; `D64_TRACE` output is identical
  at 30 and 120 fps (interpolation restores the live state after drawing).
- `MiniLoop` resets `gametic`/`ticon` for every nested loop (menus).
- The drawer must not advance tic-based state: in-between frames set `gamevbls = gametic`
  and `vblsinframe[0] = 0`; `Skyfadeback` is guarded by `I_PCInSubframe()`.
- `P_Start` runs the tag-999 line specials and adds a fade-in thinker; a save is applied
  *after* that (`G_PCApplyPendingLoad`) and replaces all things/thinkers.

## Sound banks (all big-endian)
WMD = module_header(32) + patch_group_header(24) + patch bank: patches(4 B each) |
patchmaps(20) | patchinfo(24: base, len, type, flags, pad, **pitch**, loopindex, unused) |
drummaps(4) | loopinfo(8) | raw loops(16) | ADPCM loops(48, state[16]) | ADPCM books(264, one
per sample index). Sections 8-byte aligned from the bank start. WSD = module_header + table
of 16-byte records (seq_header 12 + pointer), then per-sequence track blocks (track_header
20, label u32[], event bytes). WDD = raw VADPCM/raw16 sample data. 124 samples in USA Rev 1.
The N64 player tick is 8333 µs (120 Hz). Reverb params are an approximation.

## Savegames (src/port/savegame.c)
Raw structs with pointer fields replaced by tagged references (thing/thinker/sector/
soundorg/line(+low bit)/side/state/function/state-action/macro). Tied to struct sizes
(header check). Thinkers with `function == NULL` are ceilings/plats in stasis (found via
`activeceilings`/`activeplats`). Laser thinkers and their marker things are not saved.
`mobj->extradata` holds mobj/line|1/laser pointers or small ints.

## Resource packs (src/port/respack.c)
- N64 WAD lump names are the GZDoom/Retribution names: of 2361 doom64-rt material names,
  1175 match WAD texture (`T_START..T_END`, e.g. `C1`) or sprite (`S_START..S_END`, e.g.
  `A001A0`, `SKULA1`) lumps exactly.
- Mapping a draw back to its lump: `W_CacheLumpNum` registers the decompressed lump range;
  `gbi.c` remembers the source address of each TMEM load; `ResPack_SourceOf` validates the
  registration against `lumpcache[lump]` (zone memory gets reused).
- Wall/flat textures are loaded whole (`textureN64_t` header 8 bytes). Sprites are loaded
  in strips: strip row = (offset - 16) / (cmpsize / height); row padding columns beyond
  `width` stay transparent.
- PNG and zip-deflate decoding: vendored `src/port/third_party/stb_image.h` v2.30
  (public domain, sha1 2106632e75249329c21eab543ec02539ad94f10b).

## Testing facts
- llvmpipe (GL 4.5) and lavapipe (Vulkan incl. ray query) work under Xvfb `:99`.
- Title sequence under `D64_FIXED_TIMESTEP`: a pad press at frame ≥300 leaves the demo,
  the title menu is live at ~350, New Game + skill (presses at 380, 440) → MAP01 at ~500.
  Under Wine the skill menu takes longer to accept input: add a second A press (~520).
- Classic bug classes found so far: BE masks in `W_CheckNumForName`; `total*4` pointer
  arrays (`P_GroupLines`); stale `uls` in LoadTLUT; texrect shade alpha with the fog
  blender; freed mobjs in `RT_CollectLights` after `P_Stop`.
