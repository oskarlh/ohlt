#!/bin/sh

# Exit on failure
set -e

cp test-and-compare/valve/maps/xmastree_tjb.bsp test-and-compare/valve/maps/xmastree_tjb-first-compile.bsp
cp test-and-compare/valve/maps/xmastree_tjb-csg-only.bsp test-and-compare/valve/maps/xmastree_tjb-csg-only-first-compile.bsp

cp test-and-compare/valve/maps/pool.bsp test-and-compare/valve/maps/pool-first-compile.bsp
cp test-and-compare/valve/maps/pool-csg-only.bsp test-and-compare/valve/maps/pool-csg-only-first-compile.bsp

