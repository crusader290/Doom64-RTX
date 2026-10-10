# PORT_MANIFEST — per-file port status

File index for navigation (layer boundaries and entry points: docs/DISCOVERIES.md).

Legend: ✅ done & verified at runtime · 🟡 compiles/runs, needs more verification · 🔧 in progress · ⬜ not started · ❌ removed/replaced

## Platform layer (src/port)
| File | Status | Notes |
|---|---|---|
| include/ultra64.h | ✅ | clean-room libultra types + GBI macros; `Gfx.w1` is `uintptr_t` |
| include/libaudio.h | ✅ | alSyn*/ALHeap/ALPlayer API surface WESS uses (implemented by n64synth.c) |
| include/PR/*.h, ultratypes.h | ✅ | include shims |
| d64pc.h | ✅ | libc, BE16/BE32, virtual ROM bases, FPU no-ops, prototypes |
| os.c | ✅ | message queues, osPiStartDma -> ROM_Read, ucode identities |
| rom.c/.h | ✅ | ROM scan of cwd + exe dir (.z64/.n64/.v64, any byte order), region table, extracted-file fallback |
| config.c/.h | 🟡 | doom64rtx.ini (exe dir, else pref dir) + command line; `brightness`, `widescreen`, `log`; CONFIG_SET persists runtime changes, command-line overrides are never saved |
| input.c/.h | 🟡 | keyboard/mouse/SDL gamepad -> N64 pad word; mouse turning hook in p_user.c |
| pak.c | 🟡 | Controller Pak notes in `<prefdir>/controller.pak` |
| i_main_pc.c | ✅ | SDL3 window, Vulkan->GL fallback, 30 Hz pacing, wipes via read-back, F10 RT toggle, F11 fullscreen, F12 screenshot, test hooks |
| gu.c | ✅ | guFrustum/guMtxF2L |
| rtlights.c | 🟡 | point lights from FF_FULLBRIGHT things, flames/lamps/candles, muzzle flash; collected inside R_RenderPlayerView |
| log.c/.h | ✅ | doom64rtx.log (+ .old.log), SDL log capture, crash handler (signals + backtrace / SEH + stack), crash context, Rust panic hook |
| n64synth.c/.h | 🟡 | software N64 synth: VADPCM/raw16 voices, loops (ADPCM state restore), linear-interp resampling, equal-power pan + reverb send, linear volume ramps (vol² curve), big-room style reverb, sample-accurate player callbacks (WESS 120 Hz tick) |
| audio_pc.c | 🟡 | replaces audio.c: wess_init, ROM reads, WDD bank pointer, SDL3 audio stream (audio thread) + I_PCAudioLock; D64_WAVOUT / D64_DUMPSAMPLES hooks |
| pc_options.c/.h | ✅ | Options > Graphics / Gameplay, F7 Debug, Save/Load slot pages (data for the [PC] page system in m_main.c) |
| interp.c/.h | ✅ | 60/120 fps: snapshot before each tic, blend things/sectors/view/weapon while drawing, restore after |
| savegame.c/.h | ✅ | full level save/load with pointer references; 9 slots (7 = quick F5/F9, 8 = auto) |
| respack.c/.h, third_party/stb_image.h | ✅ | PNG resource packs (pk3 zip or folders) replacing textures/sprites by lump name; lump memory registry for gbi.c |
| gore.c/.h | ✅ | bounded cosmetic particles/gibs and floor/wall stains; own RNG, no game actors or save changes; demo/recording disabled |
| pc_text.c/.h, third_party/stb_truetype.h | ✅ | scalable Share Tech Mono atlas and ordered overlay rectangles/text; original art fallback when font unavailable |
| s_sound_stub.c | ✅ | silent sound API, only for -DD64_WITH_AUDIO=OFF |

## Graphics (src/gfx, renderer/)
| File | Status | Notes |
|---|---|---|
| src/gfx/d64gfx.h | ✅ | C ABI to the Rust renderer (mirror: renderer/src/ffi.rs) |
| src/gfx/gbi.c | ✅ | F3DEX subset, TMEM emulation (LoadBlock/LoadTile/LoadTLUT, odd-row swizzle), texture cache, combiner/blender translation, fog, rects, L3DEX lines -> quads |
| src/gfx/gfx_null.c | ✅ | headless renderer for tests |
| renderer/src/gl_backend.rs | ✅ | OpenGL 3.3 core fallback (glow) |
| renderer/src/vk/mod.rs | ✅ | ash Vulkan 1.1 raster: offscreen scene, 4 pipelines (blend x depth), descriptor cache, texture uploads, read-back, swapchain recreation |
| renderer/src/vk/mem.rs | ✅ | sub-allocator (32 MiB blocks), Buffer/Image helpers |
| renderer/src/vk/rt.rs | 🟡 | ray query world renderer: per-frame BLAS/TLAS, bindless textures, AO + bounce + shadowed lights, temporal + spatial filter, composite with depth |
| renderer/shaders/vk_rt.comp, vk_denoise.comp, vk_composite.frag | 🟡 | RT shaders (rt_common.glsl shared) |
| renderer/shaders/combiner.glsl | ✅ | N64 combiner/blender emulation shared by GL and VK |

## Game sources (src/doom64)
All files compile for x86-64. Changes are marked `[PC]`.
| File | Status | PC changes |
|---|---|---|
| i_main.c, audio.c, graph.c | ❌ | replaced by src/port (originals upstream) |
| stdarg.h, asm.h, regdef.h, wessint_s.s | ❌ | removed (MIPS / Nintendo SDK) |
| graph.h | ✅ | rewritten (debug print prototypes only) |
| doomdef.h | ✅ | includes d64pc.h; D_vsprintf -> vsprintf |
| doomdata.h | ✅ | `boolean` as int |
| w_wad.c | ✅ | name masks byte-swapped, ROM symbols via d64pc.h |
| z_zone.c, mem_heap.c | ✅ | 32 MB zone, pointer-safe Z_Init |
| p_setup.c | ✅ | linebuffer sized with sizeof(line_t*) |
| d_main.c, f_main.c, st_main.c, decodes.c, doomlib.c | ✅ | pointer casts |
| d_screens.c | ✅ | demo lumps byte-swapped |
| m_password.c | 🟡 | N64 byte order for password words (compatible passwords) |
| f_main.c, m_main.c, r_phase1/2/3.c, st_main.c | ✅ | BE16() on asset header fields |
| p_user.c | 🟡 | mouse turning hook |
| m_main.c, st_main.c, in_main.c, d_screens.c (PC UI) | ✅ | modern retro menus, Graphics/Gameplay/Audio/Controls, PC prompts/save slots; console storage/password screens bypassed |
| r_phase1.c, r_main.c | 🟡 | [PC] R_PCFovInvScale widens BSP culling for 16:9; RT_CollectLights hook |
| g_game.c | 🟡 | [PC] map load logging |
| g_game.c, r_local.h | ✅ | extern fixes |
| wess*.c, seqload*.c, funqueue.c | 🟡 | built with `-funsigned-char` (original toolchain); long->int; uintptr_t casts; [PC] prototypes; WMD/WSD headers byte-swapped (wessapi.c, seqload.c); WSD sequence table rebuilt with native records; track headers + label lists swapped while loading; CalcPartsPerInt in C |
| n64cmd.c | 🟡 | [PC] N64_PCDriverInit: swaps the patch bank and builds a native sample table (file records 24 bytes); WDD read from the ROM image; reverb controller -> alSynSetFXMix |
| s_sound.c, audio_heap.c | 🟡 | [PC] entry points take the audio lock; 1 MB audio heap; pointer casts |

## Build / packaging
| File | Status | Notes |
|---|---|---|
| CMakeLists.txt | ✅ | game + port + cargo-built Rust renderer; `-ftrivial-auto-var-init=zero`; `$ORIGIN` rpath; copies SDL3.dll on Windows |
| cmake/mingw-w64-x86_64.cmake | ✅ | Windows cross toolchain (Rust target x86_64-pc-windows-gnu) |
| .github/workflows/build.yml | 🟡 | Linux, Windows (MinGW cross), MSVC (experimental, continue-on-error) |
| tools/compile_shaders.sh | ✅ | GLSL -> SPIR-V (checked in under renderer/shaders/spv) |
| releases/ | ✅ | 0.1.1 preview archives (Linux tar.gz, Windows zip) |
| tools/make_release.sh, tools/release/ | ✅ | packaging script + ini/notes templates |
| tools/check.sh | ✅ | build entry point: preflight, incremental build, first-error excerpt, failure class, repeat guard, build-logs/ |
| tools/smoke.sh | ✅ | headless runtime test (Xvfb, ROM, presses, shots, WAV); PASS/FAIL/SKIPPED |
| tools/setup_env.sh | ✅ | idempotent environment setup (apt, rust target, prebuilt SDL3 in ../deps); `--check` |
| build.sh, build.bat, tools/build_windows.ps1 | ✅ | root launchers; Linux/Windows cross build verified; native Windows SHA256-verified portable MSYS2 setup and compile verified |
| tools/pack_visuals.py | ✅ | reproducible single PK3: sprites, two ceiling-light replacements, RT maps/defaults; per-entry origins, no ROM required |
| tools/session_status.sh, .claude/ | ✅ | SessionStart status hook; skills build-diagnose, runtime-test, session-handoff |
| tools/dm64ex.c | ref | Erick194's ROM extractor (reference for ROM offsets), not built |
