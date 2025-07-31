#!/bin/sh

# Exit on failure
set -e

export MAP_NAME=pool
export MAP_NAME=xmastree_tjb
#export MAP_NAME=hc
export MAP_NAME=example_twisted

#export CSG_ONLY=-csg-only

#export NUM_THREADS=-1

cd test-and-compare

#wine ../sdhlt-tools-win32/sdHLCSG.exe -threads 1 ./valve/maps/${MAP_NAME}
wine ../zhlt34f_x86_sse2/hlcsg.exe -threads 1 -cliptype precise ./valve/maps/${MAP_NAME}
#wine ../vluzacn_s_zhlt_v30/hlcsg.exe -threads 1 -cliptype precise ./valve/maps/${MAP_NAME}
#wine ../vluzacn_s_zhlt_v31/hlcsg.exe -threads 1 -cliptype precise ./valve/maps/${MAP_NAME}
#wine ../vluzacn-v29/hlcsg.exe -threads 1 ./valve/maps/${MAP_NAME}
#wine ../vluzacn-v12/hlcsg.exe -threads 1 -cliptype precise ./valve/maps/${MAP_NAME}
if [ "$CSG_ONLY" = "-csg-only" ]; then
	echo "CSG only"
else
#	wine ../vluzacn-v29/hlbsp.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn-v12/hlbsp.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn-v12/hlvis.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn-v12/hlrad.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn_s_zhlt_v30/hlbsp.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn_s_zhlt_v31/hlbsp.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine "/Users/oskar.larsson/Downloads/Windows XP/share4/mishmash/hlbsp.exe" -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn_s_zhlt_v30/hlvis.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../vluzacn_s_zhlt_v30/hlrad.exe -threads 1 ./valve/maps/${MAP_NAME}
wine ../zhlt34f_x86_sse2/hlbsp.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../zhlt34f_x86_sse2/hlvis.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../zhlt34f_x86_sse2/hlrad.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../sdhlt-tools-win32/sdHLBSP.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../sdhlt-tools-win32/sdHLVIS.exe -threads 1 ./valve/maps/${MAP_NAME}
#	wine ../sdhlt-tools-win32/sdHLRAD.exe -threads 1 -vismatrix sparse ./valve/maps/${MAP_NAME}
fi

cd ..


mv ./test-and-compare/valve/maps/example_twisted.bsp "/Users/oskar.larsson/Library/Application Support/Xash3D/valve/maps/example_twisted.bsp"
/Applications/Xash3D-FWGS.app/Contents/MacOS/xash3d -dev -console +map example_twisted
