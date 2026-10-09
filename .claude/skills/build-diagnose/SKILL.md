---
name: build-diagnose
description: Build Doom64-RTX and diagnose compile/link/CMake/cargo failures with tools/check.sh. Use when a build fails, before retrying a build, or when asked to compile.
---
# build-diagnose

1. `tools/check.sh <linux|win|null|all>` (incremental; `--clean` only if the cache is broken).
   Exit: 0 ok, 1 failed, 3 environment, 4 refused (same failure, unchanged sources).
2. Read only the printed excerpt first. Open `build-logs/<target>.log` with
   `grep -n` / `sed -n` around the first error if more context is needed — never cat it.
3. Classes:
   - `env`: missing tool/target/network → `tools/setup_env.sh`; do not touch code.
   - `dependency`: SDL3/cargo resolution → check `../deps/sdl3-linux`, `SDL3_DIR`,
     `rustup target list --installed`.
   - `code`: fix the first diagnostic; later ones are usually cascades. Typical ones here:
     implicit declarations (`-Werror=implicit-function-declaration`), pointer/int casts
     (`-Werror=int-to-pointer-cast`, use `uintptr_t`), header included twice
     (`p_spec.h` is already included by `p_local.h`).
   - `link`: missing definition → is the file in `CMakeLists.txt` (`D64_PORT_SOURCES`,
     `D64_AUDIO_SOURCES`)? Is a function `static`?
4. Same first error after 3 attempts (script warns): stop, re-read the code around the
   error, state a new hypothesis before the next attempt, or record a blocker in
   `docs/tasks.json`.
5. History of runs: `build-logs/history.tsv` (time, rev, inputs hash, target, result, secs,
   first error, signature).
6. Windows: `tools/check.sh win` (MinGW). Runtime check under Wine:
   `WINEPREFIX=../wineprefix wine build-win/doom64rtx.exe -gl -rom <z:\path>` with
   `D64_HEADLESS=1 D64_FIXED_TIMESTEP=1 D64_QUIT_AT=600`.
