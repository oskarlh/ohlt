#!/bin/sh

# Exit on failure
set -e

export MAP_NAME=$1
export NUM_THREADS=$2

# Change directory to the directory containing this script
cd $( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )


../tools/sdHLCSG -threads ${NUM_THREADS} ./valve/maps/${MAP_NAME}
../tools/sdHLBSP -threads ${NUM_THREADS} ./valve/maps/${MAP_NAME}

../tools/sdHLVIS -threads ${NUM_THREADS} -fast ./valve/maps/${MAP_NAME}
../tools/sdHLRAD -threads ${NUM_THREADS} -vismatrix sparse ./valve/maps/${MAP_NAME}

if cmp "./valve/maps/${MAP_NAME}.bsp" "./valve/maps/${MAP_NAME}-first-compile.bsp"; then
	echo "Compiled the map successfully :)"
else
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	exit 1
fi
