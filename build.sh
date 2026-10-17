#!/bin/sh
# Configure and build in ./build with Apple's clang and git.
# (This Mac's shell sets CC=gcc-11, and /usr/local/bin/git is an old Intel-only binary.)
set -e
cd "$(dirname "$0")"
CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGIT_EXECUTABLE=/usr/bin/git
cmake --build build -j 8
echo "built: ./build/evolution_sandbox"
