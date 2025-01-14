#!/bin/sh

# Exit on failure
set -e

export PARAMETERS_FOR_CMAKE_CONFIGURE=

# Use GCC 14
export CXX=/opt/homebrew/bin/g++-14
export PARAMETERS_FOR_CMAKE_CONFIGURE="-DCMAKE_CXX_COMPILER=$CXX"

# Use Clang 18
export CXX=$(brew --prefix llvm@18)/bin/clang++
export PARAMETERS_FOR_CMAKE_CONFIGURE="-DCMAKE_CXX_COMPILER=$CXX"

#cmake -S . --preset=debug-config
#cmake --build --preset=debug-build
cmake -S . --preset=release-config ${PARAMETERS_FOR_CMAKE_CONFIGURE}
cmake --build --preset=release-build

export MAP_NAME=pool
export MAP_NAME=xmastree_tjb

#export CSG_ONLY=-csg-only

export NUM_THREADS=1
#export NUM_THREADS=-1

./test-and-compare/test-and-compare.sh ${MAP_NAME} ${NUM_THREADS} ${CSG_ONLY}
