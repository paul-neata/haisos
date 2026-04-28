#!/bin/bash
# Build haisos for Windows from WSL by invoking the Windows batch file via cmd.exe
# Can be run from any directory
# Usage: build_windows_on_wsl.sh [debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Compute absolute path to the Windows batch script
BATCH_SCRIPT="$SCRIPT_DIR/build_windows_on_windows.bat"

if [ ! -f "$BATCH_SCRIPT" ]; then
    echo "Error: Windows batch script not found at $BATCH_SCRIPT" >&2
    exit 1
fi

# Convert Linux path to Windows path
WIN_BATCH_SCRIPT="$(wslpath -w "$BATCH_SCRIPT")"

echo "Invoking Windows build from WSL..."
echo "  Linux path:  $BATCH_SCRIPT"
echo "  Windows path: $WIN_BATCH_SCRIPT"

if [ -n "$1" ]; then
    cmd.exe /c "$WIN_BATCH_SCRIPT" "$1"
else
    cmd.exe /c "$WIN_BATCH_SCRIPT"
fi
