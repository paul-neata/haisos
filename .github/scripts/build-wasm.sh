#!/bin/bash
set -e

sudo apt-get update
sudo apt-get install -y cmake g++ libcurl4-openssl-dev

# Setup Emscripten environment
source "$EMSDK/emsdk_env.sh"

./scripts/build_wasm_on_linux.sh
