#!/bin/sh

# Exit on failure
set -e

export MAP_NAME=$1

# Change directory to the directory containing this script
cd $( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )


../tools/sdHLCSG ./valve/maps/${MAP_NAME}
../tools/sdHLBSP ./valve/maps/${MAP_NAME}
../tools/sdHLVIS -fast ./valve/maps/${MAP_NAME}
../tools/sdHLRAD -vismatrix sparse ./valve/maps/${MAP_NAME}

if cmp "./valve/maps/${MAP_NAME}.bsp" "./valve/maps/${MAP_NAME}-first-compile.bsp"; then
	echo "Compiled the map successfully :)"
else
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	exit 1
fi
