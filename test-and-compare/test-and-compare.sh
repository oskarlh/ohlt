#!/bin/sh

# Exit on failure
set -e

# Change directory to the directory containing this script
cd $( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )

# Change directory to parent dir
cd ..

cmake -S . -B build
cmake --build build

cd test-and-compare
../tools/sdHLCSG ./valve/maps/pool
../tools/sdHLBSP ./valve/maps/pool
../tools/sdHLVIS ./valve/maps/pool
../tools/sdHLRAD ./valve/maps/pool


echo Compiled the map.
echo TODO: Add a comparison here so we can detect if the .bsp is any different...
