# Builds for Linux ARM64, Linux x64 and Windows x64

Wanted: three binaries -- Linux on ARM (aarch64), Linux on x64, Windows on x64.

Today:

- Linux x64: `scripts/build_linux_on_linux.sh` builds natively, for whatever
  the host is (x86-64 so far), linking libcurl dynamically. CI's Linux job
  (`.github/workflows/ci.yml`) runs it on `ubuntu-latest`, an x64 runner.
- Windows x64: `scripts/build_windows_on_windows.bat` (run from WSL by
  `scripts/build_windows_on_wsl.sh`) configures CMake with the default Visual
  Studio generator, whose target is the host's architecture: x64 on an x64
  machine, but ARM64 on an ARM64 Windows one. CI's Windows job runs on
  `windows-latest`.
- Linux ARM64: nothing builds it.
- No architecture appears anywhere in the layout. CMake puts everything in
  `output/<platform>` (`linux`, `windows`, `wasm`, each with a `_debug`
  variant), the build trees are `build/temp_<platform>`, and `scripts/pack.sh`
  packs `bin/linux/haisos` and `bin/windows/haisos.exe` -- copying with
  `|| true`, so a missing binary still yields a package
  (`notes/note-review-low-findings.md`, section 8). An ARM64 Linux build would
  overwrite the x64 one.

To do:

- Put the architecture in every name: `output/linux_x64`, `output/linux_arm64`,
  `output/windows_x64` (CMake's platform name plus `CMAKE_SYSTEM_PROCESSOR`),
  the build trees likewise, and `bin/linux-x64`, `bin/linux-arm64`,
  `bin/windows-x64` in the package, which fails when one is missing. Update
  whatever finds binaries by the old names: the build scripts,
  `scripts/test_linux.sh` and `scripts/internal/test.js` (platform letters
  L, W, N), `scripts/pack.sh` and `pack.bat`, `.github/`, the `/build` skill,
  and the root CLAUDE.md.
- Windows: pass `-A x64` to CMake, so the target never depends on the machine
  that builds it.
- Linux ARM64, one of:
  - native, on an ARM64 machine or CI runner (GitHub's `ubuntu-24.04-arm`):
    simplest, and the tests run as they are;
  - cross-compiled on x64 with `aarch64-linux-gnu-g++` and a CMake toolchain
    file, taking libcurl from the distribution's arm64 packages (multiarch) or
    a sysroot; its tests then need `qemu-aarch64` to run;
  - built in an arm64 container on x64 (`docker buildx --platform linux/arm64`,
    emulated by QEMU): no toolchain file, but slow.
- Decide whether "Linux ARM" also means 32-bit ARM (armhf), and how portable
  the Linux binaries must be: linked dynamically against the build machine's
  glibc and libcurl, they run only where the glibc is at least as new.
