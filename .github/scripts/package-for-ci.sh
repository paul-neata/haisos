#!/bin/bash
set -e

mkdir -p output/linux
mkdir -p output/windows

# Artifacts are downloaded into output/linux/ and output/windows/ by the workflow
bash ./scripts/pack.sh
