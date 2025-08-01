#!/bin/sh

# Exit on failure
set -e

export MAP_NAME=$1
export NUM_THREADS=$2
export CSG_ONLY=$3


# Change directory to the directory containing this script
cd $( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )

if [ "$CSG_ONLY" = "-csg-only" ]; then
	cp ./valve/maps/${MAP_NAME}.map ./valve/maps/${MAP_NAME}${CSG_ONLY}.map
fi
#lldb -- program-here
../tools/hlcsg -threads ${NUM_THREADS} -noestimate ./valve/maps/${MAP_NAME}${CSG_ONLY}
if [ "$CSG_ONLY" = "-csg-only" ]; then
	echo "CSG only"
else
	../tools/hlbsp -threads ${NUM_THREADS} -noestimate ./valve/maps/${MAP_NAME}
	../tools/hlvis -threads ${NUM_THREADS} -noestimate -fast ./valve/maps/${MAP_NAME}
	../tools/hlrad -threads ${NUM_THREADS} -noestimate -vismatrix sparse ./valve/maps/${MAP_NAME}
fi
#cp "./valve/maps/${MAP_NAME}${CSG_ONLY}.bsp" "./valve/maps/${MAP_NAME}${CSG_ONLY}-first-compile.bsp"
if cmp "./valve/maps/${MAP_NAME}${CSG_ONLY}.bsp" "./valve/maps/${MAP_NAME}${CSG_ONLY}-first-compile.bsp"; then
	echo "Compiled the map successfully :)"
else
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	echo "The .bsp has changed!"
	exit 1
fi
