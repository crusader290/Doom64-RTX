cmake_minimum_required(VERSION 3.20)
project(Doom64RTX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(D64_WITH_AUDIO "Build the WESS sound system (N64 synth emulation)" ON)
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
  doomlib.c f_main.c funqueue.c g_game.c in_main.c m_bbox.c m_fixed.c
  m_main.c m_password.c p_base.c p_ceilng.c p_change.c p_doors.c p_enemy.c
  p_floor.c p_inter.c p_lights.c p_macros.c p_map.c p_maputl.c p_misc.c
  p_mobj.c p_move.c p_plats.c p_pspr.c p_setup.c p_shoot.c p_sight.c
  p_slide.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_data.c
  r_main.c r_phase1.c r_phase2.c r_phase3.c sprinfo.c st_main.c tables.c
  w_wad.c z_zone.c)
set(D64_AUDIO_SOURCES
  s_sound.c wessapi.c wessapic.c wessapim.c wessapip.c wessapit.c wessarc.c
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
  src/gfx/gbi.c
  src/gfx/gfx_null.c)
if(D64_WITH_AUDIO)
  list(APPEND D64_PORT_SOURCES src/port/n64synth.c src/port/audio_pc.c)
else()
  list(APPEND D64_PORT_SOURCES src/port/s_sound_stub.c)
endif()

add_executable(doom64rtx ${D64_GAME_SOURCES} ${D64_PORT_SOURCES})
if(D64_WITH_AUDIO)
  target_sources(doom64rtx PRIVATE ${D64_AUDIO_SOURCES})
endif()

target_include_directories(doom64rtx PRIVATE
  ${CMAKE_SOURCE_DIR}/src/port/include
  ${CMAKE_SOURCE_DIR}/src/port
  ${CMAKE_SOURCE_DIR}/src/gfx
  ${D64_GAME_DIR})
target_compile_definitions(doom64rtx PRIVATE D64_PC=1 F3DEX_GBI=1 NDEBUG _FINALROM)
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
  if(NOT WIN32)
    target_link_libraries(doom64rtx PRIVATE m)
  endif()
elseif(MSVC)
  target_compile_options(doom64rtx PRIVATE /W3 /wd4244 /wd4018 /wd4996 /wd4146)
endif()

if(WIN32)
  set_target_properties(doom64rtx PROPERTIES WIN32_EXECUTABLE OFF)
endif()
