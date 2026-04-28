#!/bin/bash
# Build haisos for Linux on a Linux system
# Usage: build_linux_on_linux.sh [debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="Release"
BUILD_SUFFIX=""
if [ "$1" = "debug" ]; then
    BUILD_TYPE="Debug"
    BUILD_SUFFIX="_debug"
    echo "Building haisos for Linux (Debug)..."
else
    echo "Building haisos for Linux..."
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

# Create build directory if it doesn't exist
mkdir -p "build/temp_linux${BUILD_SUFFIX}"

# Configure and build
cmake -B "build/temp_linux${BUILD_SUFFIX}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DHAISOS_DEBUG=$([ "$BUILD_TYPE" = "Debug" ] && echo ON || echo OFF)
cmake --build "build/temp_linux${BUILD_SUFFIX}"

# Print the final command to run
echo ""
echo "Build complete! Run with:"
echo "$PROJECT_DIR/output/linux${BUILD_SUFFIX}/haisos --help"
