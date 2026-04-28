---
name: test
description: Run project tests with platform, test type, configuration, and filter selection
---

Run the project tests using the unified test runners.

**Arguments:** [platform] [selector] [options] [filter]

- **platform**: combination of W (Windows), L (Linux), N (WASM/Node), or `*` for all. Case-insensitive.
- **selector**: combination of U (unit), I (integration), H (haisos), or `*` for all. Case-insensitive.
- **options**: `--debug` to run debug only, `--both` to run debug and release.
- **filter**: optional string to filter test names.

**Default behavior (no arguments):**
Both scripts default to the current platform and all test types when invoked without arguments.

**Script selection:**
- On Windows (native): invoke `scripts/test_windows.bat`
- On Linux or WSL: invoke `scripts/test_linux.sh`

Pass all provided arguments through to the selected script unchanged.

When running from WSL and the platform includes W, `test_linux.sh` will automatically delegate Windows tests to `test_windows.bat` via `cmd.exe`.

Examples:
- `/test` → runs all tests for current platform
- `/test L U` → Linux unit tests (release)
- `/test W UI --debug` → Windows unit + integration tests (debug)
- `/test * * --both` → all platforms, all test types, both debug and release
