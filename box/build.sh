#!/bin/bash
# Build the brushie CLI on a box. Usage: ssh <box> "bash -s" < box/build.sh
set -e
cd /content/brushie
mkdir -p build
clang++ -std=c++17 -O3 -ffast-math -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
echo "built $(pwd)/build/brushie"
