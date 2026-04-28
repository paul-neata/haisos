#!/bin/bash
set -e

sudo apt-get update
sudo apt-get install -y cmake g++ libcurl4-openssl-dev

./scripts/build_linux_on_linux.sh
