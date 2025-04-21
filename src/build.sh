#!/bin/bash
# This script builds the C extension for the mapgen module.
# It is purposefully simple and does not use any build system.
#gcc -std=c99 -O3 -march=native -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c
gcc -std=c99 -O3 -march=native -ffast-math -funroll-loops -mfma -mavx2  -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -o geod geod.c -lpng -ldl -lpthread -lm

#gcc -std=c99 -O3 -march=haswell -ffast-math -funroll-loops -mfma -mavx2
#
# PLUGINS
#
gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o http_hello.so plugin_http_hello/plugin_http_hello.c
mv http_hello.so ../plugins/http_hello.so

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o shape.so plugin_shape/plugin_shape.c plugin_shape/shape.c plugin_shape/plan.c
mv shape.so ../plugins/shape.so


mv geod ../bin/geod