#!/bin/bash
# This script builds the C extension for the mapgen module.
# It is purposefully simple and does not use any build system.
#gcc -std=c99 -O3 -march=native -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c
gcc -std=c99 -O3 -march=native -ffast-math -funroll-loops -mfma -mavx2  -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c


#gcc -std=c99 -O3 -march=haswell -ffast-math -funroll-loops -mfma -mavx2
