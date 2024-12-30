#!/bin/sh

# Exit on failure
set -e

# Use GCC 14
# export CC=/opt/homebrew/bin/gcc-14
# export CXX=/opt/homebrew/bin/g++-14
# -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX

cmake -S . --preset=release-config
cmake --build --preset=release-build


export MAP_NAME=pool
export MAP_NAME=xmastree_tjb

export NUM_THREADS=1
#export NUM_THREADS=-1

./test-and-compare/test-and-compare.sh ${MAP_NAME} ${NUM_THREADS}
