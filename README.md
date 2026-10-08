# Doom64-RTX

A native PC port of **Doom 64**, built from [Erick194's DOOM64-RE](https://github.com/Erick194/DOOM64-RE)
reverse-engineered source, with:

- **SDL3** for windowing, input (keyboard, mouse, gamepads) and audio output
- a **Vulkan 1.1+ renderer written in Rust with [ash](https://github.com/ash-rs/ash)**
- an optional **ray traced world renderer** (Vulkan `VK_KHR_ray_query`) you can switch on and off
- an **OpenGL 3.3 fallback** renderer (Rust, [glow](https://github.com/grovesNL/glow)) used automatically
  when Vulkan is not available
- targets **x86-64 Windows and Linux**

The ray tracing approach takes its ideas from [doom64-rt](https://github.com/jlrouzies-fr/doom64-rt)
(real light sources instead of painted light, sector light colours kept as the art direction,
temporal accumulation and denoising). No code is shared with that project.

> **You need your own copy of Doom 64.** No game data is included or downloaded.

![Title demo, OpenGL renderer](docs/img/gl_title_demo.png)

## Download

Preview builds are in [`releases/`](releases/):

| File | Platform |
|---|---|
| `doom64rtx-0.1.0-windows-x86_64.zip` | Windows 10/11 x86-64 (`SDL3.dll` included) |
| `doom64rtx-0.1.0-linux-x86_64.tar.gz` | Linux x86-64, glibc 2.39+ (`libSDL3.so.0` included) |

Extract, put your ROM next to the executable, run `doom64rtx`. CI artifacts from
`.github/workflows/build.yml` are built the same way.

## Status

| Area | State |
|---|---|
| Game code (DOOM64-RE) compiled for 64-bit PC | working |
| ROM auto-detection | working |
| OpenGL 3.3 renderer | working |
| Vulkan raster renderer | working |
| Vulkan ray traced world | preview (F10); light sources from game objects in progress |
| Sound and music (WESS / N64 synth) | in progress (silent for now) |
| Windows build (MinGW-w64) | working (tested under Wine: OpenGL and Vulkan) |
| Saves (Controller Pak emulation) | working, needs more testing |

See [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) for the detailed log and
[PORT_MANIFEST.md](PORT_MANIFEST.md) for per-file status.

## Playing

1. Put your Doom 64 ROM (`.z64`, `.n64` or `.v64`, any byte order) in the folder you start the
   game from, or next to the executable. Supported dumps: USA, USA Rev 1, Europe, Japan.
   The ROM is detected automatically; you can also pass `-rom <file>` or set `rom=` in the ini.
   Pre-extracted `DOOM64.WAD/.WMD/.WSD/.WDD` files in the same places also work.
2. Run `doom64rtx` (`doom64rtx.exe` on Windows).

### Command line

| Option | Effect |
|---|---|
| `-rom <file>` | use this ROM image |
| `-vulkan` / `-gl` | choose the renderer (Vulkan falls back to OpenGL automatically) |
| `-rt` / `-nort` | ray tracing on / off |
| `-fullscreen` / `-window` | display mode |
| `-set <name> <value>` | set any ini setting for this run |

### Keys

| Key | Action |
|---|---|
| W / S, Up / Down | forward / back |
| Left / Right, mouse | turn |
| A / D | strafe |
| Ctrl, left mouse | fire |
| E, Space, right mouse | use |
| Shift | run |
| Tab | automap |
| Q / mouse wheel | previous / next weapon |
| Enter / Backspace | menu confirm / back |
| Esc | menu |
| **F10** | **toggle ray tracing** (Vulkan, RT-capable GPU) |
| F11, Alt+Enter | fullscreen |
| F12 | screenshot |

Gamepads use an N64-like layout: left stick moves and turns, right stick turns, right
trigger fires, shoulders strafe, A/B confirm and back, X uses, Y opens the map.

### Settings (`doom64rtx.ini`)

Read from next to the executable if present (portable install), otherwise from the user's
preference directory (`%APPDATA%\Doom64RTX\Doom64RTX\` or `~/.local/share/Doom64RTX/Doom64RTX/`).

```ini
renderer = vulkan        # vulkan | opengl
raytracing = 0           # 1 = ray traced world (also toggled with F10)
rt_spp = 1               # rays per pixel for AO / bounce light
rt_bounces = 1
rt_denoise = 1
rt_light_scale = 1
fullscreen = 0
width = 1280
height = 960
vsync = 1
mouse = 1
mouse_sens = 1
gpu_index = -1           # -1 = pick automatically
validation = 0           # Vulkan validation layers
```

Controller Pak saves are stored as `controller.pak` in the preference directory.

## Building

Requirements: CMake 3.20+, a C11 compiler (GCC, Clang or MSVC), Rust (stable) with `cargo`,
and SDL3 (found on the system, otherwise downloaded and built automatically).
`glslangValidator` is only needed if you change the Vulkan shaders (compiled SPIR-V is checked in;
run `tools/compile_shaders.sh`).

### Linux

```sh
# Debian/Ubuntu build dependencies for SDL3
sudo apt install build-essential cmake ninja-build libx11-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libegl-dev \
    libgl-dev libasound2-dev libpulse-dev libdecor-0-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/doom64rtx
```

### Windows

Experimental (CI runs it, not yet verified) with Visual Studio 2022 and Rust (`x86_64-pc-windows-msvc`):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Supported: cross-compile from Linux with MinGW-w64 (`rustup target add x86_64-pc-windows-gnu`):

```sh
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build-win
```

Copy `SDL3.dll` next to `doom64rtx.exe`.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `D64_WITH_AUDIO` | OFF | build the WESS sound system (being ported) |
| `D64_NULL_RENDERER` | OFF | headless renderer for tests (no Rust needed) |
| `D64_FETCH_SDL3` | ON | download SDL3 if it is not installed |

## How it works

The original game builds Nintendo 64 display lists for the RSP/RDP. This port keeps that code
and replaces the hardware:

```
DOOM64-RE game code ──► display lists (F3DEX encoding, 64-bit safe)
        │                        │
src/port: SDL3 main loop,        ▼
ROM loader, input, pak    src/gfx/gbi.c: RSP/RDP interpreter
                          (matrices, TMEM, texture decode, combiner/blender state)
                                 │  d64gfx.h (C ABI)
                                 ▼
                 renderer/ (Rust): Vulkan (ash) raster ── optional ray traced world
                                   OpenGL 3.3 fallback (glow)
```

The N64 colour combiner and blender are emulated in a shared GLSL routine, so menus, HUD,
sky and world look like the original. In ray traced mode the 3D world is traced instead of
rasterised and composited in the original draw order.

## Testing without a GPU

Mesa's software drivers (llvmpipe for OpenGL, lavapipe for Vulkan including ray queries) run the
game under Xvfb. Test hooks such as `D64_SHOTS`, `D64_QUIT_AT` and `D64_FIXED_TIMESTEP` are
described in [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md).

## Credits and license

- Doom 64 reverse engineering: **Erick Vásquez García (GEC)** and contributors, DOOM64-RE (GPLv3).
  See [docs/DOOM64-RE-CONTRIBUTORS.md](docs/DOOM64-RE-CONTRIBUTORS.md).
- Ray tracing reference: [doom64-rt](https://github.com/jlrouzies-fr/doom64-rt) by jlrouzies.
- Doom 64 © id Software / Midway. This project contains no game assets.

Licensed under the **GNU GPL v3**, the same as DOOM64-RE. See [LICENSE](LICENSE).
