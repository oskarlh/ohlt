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
../tools/sdHLVIS -fast ./valve/maps/pool
../tools/sdHLRAD ./valve/maps/pool

if cmp "./valve/maps/pool.bsp" "./valve/maps/pool-first-compile.bsp"; then
	echo "Compiled the map successfully :)"
else
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	exit 1
fi
