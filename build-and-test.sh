#!/bin/sh

# Exit on failure
set -e

export DEBUG_OR_RELEASE=debug
export DEBUG_OR_RELEASE=release

export COMPILER=default
#export COMPILER=linux-gcc14
export COMPILER=macos-clang18-homebrew
#export COMPILER=macos-gcc14-homebrew
#export COMPILER=windows-msvc

cmake -S . --preset=${DEBUG_OR_RELEASE}-${COMPILER}
cmake --build --preset=${DEBUG_OR_RELEASE}-${COMPILER}

export MAP_NAME=pool
export MAP_NAME=xmastree_tjb
#export MAP_NAME=hc

#export CSG_ONLY=-csg-only

export NUM_THREADS=1
#export NUM_THREADS=-1

./test-and-compare/test-and-compare.sh ${MAP_NAME} ${NUM_THREADS} ${CSG_ONLY}
