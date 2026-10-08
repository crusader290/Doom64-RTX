# PROJECT_CONTEXT — Doom64-RTX

Handoff log for any agent or human picking this up. **Update it every session.** The newest
entries go at the top of the *Session log*.

## Goal (from the user)
1. Take the Doom 64 reverse-engineering source (https://github.com/Erick194/DOOM64-RE, GPLv3)
   and **compile it as a native PC program**.
2. Platform layer: **SDL3** (window, input, audio, timing).
3. Renderers: **Vulkan** (primary) with **ray tracing**, plus an **OpenGL fallback**.
4. Ray tracing approach: use https://github.com/jlrouzies-fr/doom64-rt as the *reference* for
   what to light and how (real emitters instead of baked light, sector-light driven lighting,
   denoise/accumulate). That project is GZDoom-RT + RTGL1 on Retribution; we do not share
   code with it, we take ideas.
5. Targets: **x86-64 Windows and Linux**.
6. **ROM detection**: the game scans its working directory for a Doom 64 ROM
   (`.z64/.n64/.v64`), fixes the byte order, and pulls WAD/WMD/WSD/WDD out of it in memory.
7. Keep `PROJECT_CONTEXT.md` (this file) and `PORT_MANIFEST.md` (per-file port status) logged.

## Repo layout
```
src/doom64/     Original DOOM64-RE game sources (modified for PC; every change is
                marked with a `// [PC]` comment)
src/port/       Platform layer: libultra shim (ultra64.h + os*), SDL3 main loop, input,
                audio output, ROM loader/detector, config
src/gfx/        N64 display list (F3DEX GBI) interpreter + render backends
                (gl = OpenGL 3.3 fallback, vk = Vulkan raster, vkrt = Vulkan ray tracing)
src/gfx/shaders GLSL sources; compiled SPIR-V is committed as headers (see below)
tools/          dm64ex.c — Erick194's ROM extractor (reference for ROM offsets)
.github/        CI: Linux (gcc) and Windows (MinGW cross + MSVC) builds
```

## Key facts / discoveries
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

## Build
See README.md. Short version:
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/doom64rtx        # with a Doom 64 ROM in the working directory
```
Windows cross-compile from Linux: `cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake`.

## Testing without a GPU (works in the cloud container)
Mesa's llvmpipe (OpenGL 4.5) and lavapipe (Vulkan 1.4 **with VK_KHR_ray_query and
VK_KHR_acceleration_structure**) run under Xvfb, so every backend can be exercised headless:
```
Xvfb :99 -screen 0 1280x1024x24 &
cd <dir with the ROM>
DISPLAY=:99 D64_HEADLESS=1 D64_SHOTS=200,500 D64_QUIT_AT=501 ./doom64rtx -gl     # or -vulkan, -rt
```
Test hooks (src/port/i_main_pc.c): `D64_SHOTS` = frames to save as shot_NNNN.bmp,
`D64_QUIT_AT` = exit at frame, `D64_PRESS="frame:hexmask[/len],..."` = tap N64 buttons
(mask = high 16 bits of the pad word, e.g. 1000 = START, 8000 = A), `D64_HEADLESS` = no
message boxes, `D64_DUMPTEX` = dump every decoded texture as .pam (gbi.c).
`-DD64_NULL_RENDERER=ON` builds without the Rust renderer (prints frame stats).

## Session log
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
- Container setup used: `apt-get install glslang-tools mingw-w64 libvulkan-dev libx11-dev
  libxext-dev libwayland-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev`.
