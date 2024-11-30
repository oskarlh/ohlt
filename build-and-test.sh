#!/bin/sh

# Exit on failure
set -e

cmake -S . -B build
cmake --build build
./test-and-compare/test-and-compare.sh


