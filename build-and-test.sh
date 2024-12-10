#!/bin/sh

# Exit on failure
set -e

cmake -S . --preset=release-config
cmake --build --preset=release-build


export MAP_NAME=pool
export MAP_NAME=xmastree_tjb

export NUM_THREADS=1
#export NUM_THREADS=-1

./test-and-compare/test-and-compare.sh ${MAP_NAME} ${NUM_THREADS}
