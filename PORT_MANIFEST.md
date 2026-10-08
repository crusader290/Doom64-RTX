# PORT_MANIFEST — per-file port status

Legend: ✅ done & verified at runtime · 🟡 compiles/runs, needs more verification · 🔧 in progress · ⬜ not started · ❌ removed/replaced

## Platform layer (src/port)
| File | Status | Notes |
|---|---|---|
| include/ultra64.h | ✅ | clean-room libultra types + GBI macros; `Gfx.w1` is `uintptr_t` |
| include/libaudio.h | 🟡 | types for WESS; implementation pending (task: audio) |
| include/PR/*.h, ultratypes.h | ✅ | include shims |
| d64pc.h | ✅ | libc, BE16/BE32, virtual ROM bases, FPU no-ops, prototypes |
| os.c | ✅ | message queues, osPiStartDma -> ROM_Read, ucode identities |
| rom.c/.h | ✅ | ROM scan of cwd + exe dir (.z64/.n64/.v64, any byte order), region table, extracted-file fallback |
| config.c/.h | 🟡 | doom64rtx.ini (exe dir, else pref dir) + command line |
| input.c/.h | 🟡 | keyboard/mouse/SDL gamepad -> N64 pad word; mouse turning hook in p_user.c |
| pak.c | 🟡 | Controller Pak notes in `<prefdir>/controller.pak` |
| i_main_pc.c | ✅ | SDL3 window, Vulkan->GL fallback, 30 Hz pacing, wipes via read-back, F10 RT toggle, F11 fullscreen, F12 screenshot, test hooks |
| gu.c | ✅ | guFrustum/guMtxF2L |
| rtlights.c | ⬜ | stub; will collect emitters for RT |
| s_sound_stub.c | ✅ | silent sound API while WESS is ported |

## Graphics (src/gfx, renderer/)
| File | Status | Notes |
|---|---|---|
| src/gfx/d64gfx.h | ✅ | C ABI to the Rust renderer (mirror: renderer/src/ffi.rs) |
| src/gfx/gbi.c | ✅ | F3DEX subset, TMEM emulation (LoadBlock/LoadTile/LoadTLUT, odd-row swizzle), texture cache, combiner/blender translation, fog, rects, L3DEX lines -> quads |
| src/gfx/gfx_null.c | ✅ | headless renderer for tests |
| renderer/src/gl_backend.rs | ✅ | OpenGL 3.3 core fallback (glow) |
| renderer/src/vk/ | 🔧 | ash Vulkan 1.1 raster + ray query path |
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
| g_game.c, r_local.h | ✅ | extern fixes |
| wess*.c, seqload*.c, n64cmd.c, s_sound.c, funqueue.c | ⬜ | not built yet (D64_WITH_AUDIO=OFF) |
