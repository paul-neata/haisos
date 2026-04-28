---
name: build
description: Build the project for the current or selected platform(s) in release or debug mode.
---

Build the Haisos project using the platform-specific build scripts.

**Arguments:** [platform] [options]

- **platform**: `L` (Linux), `W` (Windows), `N` (WASM/Node), or `*` for all. Case-insensitive. Defaults to the current platform.
- **options**: `--debug` to build debug configuration. Defaults to release.

## Steps

1. Parse arguments. The first positional argument (if any, and not starting with `--`) is the platform selector. Remaining arguments are options.
2. Determine the current platform by running:
   ```bash
   if [[ "$OS" == "Windows_NT" ]] || [[ "$(uname -s)" == MINGW* ]] || [[ "$(uname -s)" == MSYS* ]] || [[ "$(uname -s)" == CYGWIN* ]]; then
       CURRENT_PLATFORM="W"
   elif [[ "$(uname -s)" == Linux* ]]; then
       CURRENT_PLATFORM="L"
   else
       CURRENT_PLATFORM="L"
   fi
   ```
   Also detect WSL:
   ```bash
   IS_WSL=0
   if [[ "$(uname -s)" == Linux* ]] && grep -qi microsoft /proc/version 2>/dev/null; then
       IS_WSL=1
   fi
   ```
3. Determine target platforms from the platform argument (case-insensitive):
   - If no platform argument is provided, use the current platform.
   - If `*`, build all platforms: Linux, Windows (if on WSL), and WASM.
   - Otherwise, build for each letter found (`L`, `W`, `N`).
4. Determine configuration:
   - If `--debug` is in the arguments, set `BUILD_ARG="debug"`.
   - Otherwise, `BUILD_ARG=""`.
5. Build each selected platform by invoking the matching script with the `BUILD_ARG`:
   - **Linux (`L`)**: run `bash scripts/build_linux_on_linux.sh "$BUILD_ARG"`
   - **WASM (`N`)**: run `bash scripts/build_wasm_on_linux.sh "$BUILD_ARG"`
   - **Windows (`W`)**:
     - If `IS_WSL == 1`: run `bash scripts/build_windows_on_wsl.sh "$BUILD_ARG"`
     - Else if `CURRENT_PLATFORM == "W"`: run `cmd.exe /c scripts/build_windows_on_windows.bat "$BUILD_ARG"`
     - Otherwise, report that Windows builds are only available from Windows or WSL.
6. Report success or failure per platform. If any build fails, report failure overall.

## Examples

- `/build` → build current platform, release
- `/build L` → build Linux, release
- `/build W --debug` → build Windows, debug
- `/build * --debug` → build all platforms, debug
- `/build LN` → build Linux and WASM, release
