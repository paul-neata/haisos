#!/bin/bash
# Build haisos for WASM using Emscripten
# Usage: build_wasm_on_linux.sh [debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="Release"
BUILD_SUFFIX=""
if [ "$1" = "debug" ]; then
    BUILD_TYPE="Debug"
    BUILD_SUFFIX="_debug"
    echo "Building haisos for WASM (Debug)..."
else
    echo "Building haisos for WASM..."
fi

cd "$PROJECT_DIR"

# Clone external dependencies if missing
if [ ! -d "extern/nlohmann_json" ]; then
    echo "Cloning nlohmann/json..."
    git clone --branch v3.11.3 --depth 1 https://github.com/nlohmann/json.git extern/nlohmann_json
fi

if [ ! -d "extern/googletest" ]; then
    echo "Cloning google/googletest..."
    git clone --branch v1.14.0 --depth 1 https://github.com/google/googletest.git extern/googletest
fi

BUILD_DIR="build/temp_wasm${BUILD_SUFFIX}"
mkdir -p "$BUILD_DIR"

# Configure only on first run; reuse existing build system on rebuilds
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DHAISOS_DEBUG=$([ "$BUILD_TYPE" = "Debug" ] && echo ON || echo OFF)
fi

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# Print the final command to run
echo ""
echo "Build complete! Run with:"
echo "    node $PROJECT_DIR/output/wasm${BUILD_SUFFIX}/haisos.js"
