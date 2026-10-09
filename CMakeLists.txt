cmake_minimum_required(VERSION 3.20)
project(Doom64RTX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(D64_WITH_AUDIO "Build the WESS sound system (N64 synth emulation)" ON)
option(D64_NULL_RENDERER "Link the headless null renderer instead of the Rust renderer" OFF)
option(D64_FETCH_SDL3 "Download and build SDL3 if it is not found" ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "32-bit builds are not supported; target x86-64")
endif()

# ---------------------------------------------------------------- SDL3
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  if(D64_FETCH_SDL3)
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.2.24
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  else()
    message(FATAL_ERROR "SDL3 not found (set SDL3_DIR or enable D64_FETCH_SDL3)")
  endif()
endif()

# ---------------------------------------------------------------- game
set(D64_GAME_DIR ${CMAKE_SOURCE_DIR}/src/doom64)
set(D64_GAME_SOURCES
  am_main.c c_convert.c cfb.c d_main.c d_screens.c decodes.c doominfo.c
  doomlib.c f_main.c g_game.c in_main.c m_bbox.c m_fixed.c
  m_main.c m_password.c p_base.c p_ceilng.c p_change.c p_doors.c p_enemy.c
  p_floor.c p_inter.c p_lights.c p_macros.c p_map.c p_maputl.c p_misc.c
  p_mobj.c p_move.c p_plats.c p_pspr.c p_setup.c p_shoot.c p_sight.c
  p_slide.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_data.c
  mem_heap.c audio_heap.c vsprintf.c r_main.c r_phase1.c r_phase2.c r_phase3.c sprinfo.c st_main.c tables.c
  w_wad.c z_zone.c)
set(D64_AUDIO_SOURCES
  s_sound.c funqueue.c wessapi.c wessapic.c wessapim.c wessapip.c wessapit.c wessarc.c
  wessedit.c wessfade.c wesshand.c wessseq.c wessshell.c wesstrak.c
  wesstwek.c seqload.c seqloadl.c seqloadr.c n64cmd.c)
list(TRANSFORM D64_GAME_SOURCES PREPEND ${D64_GAME_DIR}/)
list(TRANSFORM D64_AUDIO_SOURCES PREPEND ${D64_GAME_DIR}/)

set(D64_PORT_SOURCES
  src/port/i_main_pc.c
  src/port/os.c
  src/port/rom.c
  src/port/input.c
  src/port/pak.c
  src/port/config.c
  src/port/gu.c
  src/port/rtlights.c
  src/port/log.c
  src/port/pc_options.c
  src/port/interp.c
  src/port/savegame.c
  src/gfx/gbi.c
)
if(D64_NULL_RENDERER)
  list(APPEND D64_PORT_SOURCES src/gfx/gfx_null.c)
endif()
if(D64_WITH_AUDIO)
  list(APPEND D64_PORT_SOURCES src/port/n64synth.c src/port/audio_pc.c)
else()
  list(APPEND D64_PORT_SOURCES src/port/s_sound_stub.c)
endif()

add_executable(doom64rtx ${D64_GAME_SOURCES} ${D64_PORT_SOURCES})

# ---------------------------------------------------------------- renderer (Rust)
if(NOT D64_NULL_RENDERER)
  find_program(CARGO_EXECUTABLE cargo HINTS $ENV{HOME}/.cargo/bin REQUIRED)
  if(NOT D64_RUST_TARGET)
    if(WIN32 AND MSVC)
      set(D64_RUST_TARGET x86_64-pc-windows-msvc)
    elseif(WIN32)
      set(D64_RUST_TARGET x86_64-pc-windows-gnu)
    endif()
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(D64_CARGO_PROFILE debug)
    set(D64_CARGO_FLAGS "")
  else()
    set(D64_CARGO_PROFILE release)
    set(D64_CARGO_FLAGS --release)
  endif()
  set(D64_CARGO_TARGET_DIR ${CMAKE_BINARY_DIR}/cargo)
  if(D64_RUST_TARGET)
    set(D64_CARGO_TARGET_FLAG --target ${D64_RUST_TARGET})
    set(D64_RUST_OUT ${D64_CARGO_TARGET_DIR}/${D64_RUST_TARGET}/${D64_CARGO_PROFILE})
  else()
    set(D64_CARGO_TARGET_FLAG "")
    set(D64_RUST_OUT ${D64_CARGO_TARGET_DIR}/${D64_CARGO_PROFILE})
  endif()
  if(MSVC)
    set(D64_RUST_LIB ${D64_RUST_OUT}/d64gfx.lib)
  else()
    set(D64_RUST_LIB ${D64_RUST_OUT}/libd64gfx.a)
  endif()
  file(GLOB_RECURSE D64_RUST_SOURCES ${CMAKE_SOURCE_DIR}/renderer/src/*.rs
       ${CMAKE_SOURCE_DIR}/renderer/shaders/*)
  add_custom_command(
    OUTPUT ${D64_RUST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${D64_CARGO_TARGET_DIR}
            ${CARGO_EXECUTABLE} build ${D64_CARGO_FLAGS} ${D64_CARGO_TARGET_FLAG}
            --manifest-path ${CMAKE_SOURCE_DIR}/renderer/Cargo.toml
    DEPENDS ${D64_RUST_SOURCES} ${CMAKE_SOURCE_DIR}/renderer/Cargo.toml
    COMMENT "Building Rust renderer (d64gfx)"
    USES_TERMINAL)
  add_custom_target(d64gfx_build DEPENDS ${D64_RUST_LIB})
  add_dependencies(doom64rtx d64gfx_build)
  target_link_libraries(doom64rtx PRIVATE ${D64_RUST_LIB})
  if(WIN32)
    target_link_libraries(doom64rtx PRIVATE ws2_32 userenv bcrypt ntdll advapi32 kernel32 opengl32)
  else()
    target_link_libraries(doom64rtx PRIVATE dl pthread m)
  endif()
endif()
if(D64_WITH_AUDIO)
  target_sources(doom64rtx PRIVATE ${D64_AUDIO_SOURCES})
  # WESS was built with an unsigned-char compiler (it tests char data
  # against 0xFF), so keep that for the sound system.
  if(MSVC)
    set_source_files_properties(${D64_AUDIO_SOURCES} PROPERTIES COMPILE_OPTIONS "/J")
  else()
    set_source_files_properties(${D64_AUDIO_SOURCES} PROPERTIES COMPILE_OPTIONS "-funsigned-char")
  endif()
endif()

target_include_directories(doom64rtx PRIVATE
  ${CMAKE_SOURCE_DIR}/src/port/include
  ${CMAKE_SOURCE_DIR}/src/port
  ${CMAKE_SOURCE_DIR}/src/gfx
  ${D64_GAME_DIR})
set(D64_VERSION "0.2.0")
find_package(Git QUIET)
set(D64_GIT_HASH "unknown")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE D64_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
target_compile_definitions(doom64rtx PRIVATE D64_PC=1 F3DEX_GBI=1 NDEBUG _FINALROM
  D64_VERSION="${D64_VERSION}" D64_GIT_HASH="${D64_GIT_HASH}")
target_link_libraries(doom64rtx PRIVATE SDL3::SDL3)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # The original code relies on wrapping integer overflow and loose aliasing.
  target_compile_options(doom64rtx PRIVATE -fwrapv -fno-strict-aliasing
    -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
    -Wno-missing-braces -Wno-parentheses -Wno-pointer-sign -Wno-char-subscripts
    -Wno-unused-value -Wno-dangling-else -Wno-misleading-indentation
    -Wno-unknown-pragmas -Wno-multichar
    -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=implicit-function-declaration
    -Werror=return-type -Werror=incompatible-pointer-types)
  # The reconstructed code reads some locals before writing them (see
  # PROJECT_CONTEXT.md, "determinism"); zero them so behaviour is identical
  # on every run and every renderer.
  include(CheckCCompilerFlag)
  check_c_compiler_flag(-ftrivial-auto-var-init=zero D64_HAVE_AUTOVAR_INIT)
  if(D64_HAVE_AUTOVAR_INIT)
    target_compile_options(doom64rtx PRIVATE -ftrivial-auto-var-init=zero)
  else()
    message(WARNING "compiler lacks -ftrivial-auto-var-init=zero; demos may desync")
  endif()
  if(NOT WIN32)
    target_link_libraries(doom64rtx PRIVATE m)
  endif()
elseif(MSVC)
  target_compile_options(doom64rtx PRIVATE /W3 /wd4244 /wd4018 /wd4996 /wd4146)
endif()

if(UNIX AND NOT APPLE)
  # look for a bundled libSDL3.so next to the executable first
  set_target_properties(doom64rtx PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
if(WIN32)
  set_target_properties(doom64rtx PROPERTIES WIN32_EXECUTABLE OFF)
  # ship SDL3.dll next to the executable
  add_custom_command(TARGET doom64rtx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:doom64rtx>)
  if(MINGW)
    target_link_options(doom64rtx PRIVATE -static-libgcc)
  endif()
endif()
