#!/bin/sh

# Exit on failure
set -e

export DEBUG_OR_RELEASE=debug
export DEBUG_OR_RELEASE=release

export COMPILER=default
#export COMPILER=linux-gcc15
export COMPILER=macos-clang18-homebrew
#export COMPILER=macos-gcc15-homebrew
#export COMPILER=windows-msvc

cmake -S . --preset=${DEBUG_OR_RELEASE}-${COMPILER}
cmake --build --preset=${DEBUG_OR_RELEASE}-${COMPILER}

export MAP_NAME=pool
export MAP_NAME=xmastree_tjb
#export MAP_NAME=hc
#export MAP_NAME=example_twisted

#export CSG_ONLY=-csg-only

export NUM_THREADS=1
#export NUM_THREADS=-1

./test-and-compare/test-and-compare.sh ${MAP_NAME} ${NUM_THREADS} ${CSG_ONLY} #|| echo "test-and-compare failed"

mv ./test-and-compare/valve/maps/${MAP_NAME}.bsp "/Users/oskar.larsson/Library/Application Support/Xash3D/valve/maps/${MAP_NAME}.bsp"
#/Applications/Xash3D-FWGS.app/Contents/MacOS/xash3d -dev -console +map ${MAP_NAME}
