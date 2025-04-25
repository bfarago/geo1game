#!/bin/bash
# This script builds the C extension for the mapgen module.
# It is purposefully simple and does not use any build system.
#gcc -std=c99 -O3 -march=native -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c
gcc -std=c99 -O3 -march=native -ffast-math -funroll-loops -mfma -mavx2  -shared -fPIC -o libmapgen_c.so mapgen.c perlin3d.c
cp libmapgen_c.so ../bin/libmapgen_c.so
gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -o geod config.c http.c geod.c -lpng -ldl -lpthread -lm

#gcc -std=c99 -O3 -march=haswell -ffast-math -funroll-loops -mfma -mavx2
#
# PLUGINS
#
gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o http_hello.so plugin_http_hello/plugin_http_hello.c
mv http_hello.so ../plugins/http_hello.so

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o image.so plugin_image/plugin_image.c -lpng
mv image.so ../plugins/image.so

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o texture.so plugin_texture/plugin_texture.c -lm
mv texture.so ../plugins/texture.so
gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o localmap.so plugin_texture/plugin_localmap.c -lm
mv localmap.so ../plugins/localmap.so

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o region.so plugin_region/plugin_region.c
mv region.so ../plugins/region.so

gcc -Wall -Wextra -g -std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -fPIC -shared -I./ -o shape.so plugin_shape/plugin_shape.c plugin_shape/shape.c plugin_shape/plan.c
mv shape.so ../plugins/shape.so

gcc -fPIC -shared -g -std=c99 -O0 -o db_mysql.so plugin_db/plugin_mysql.c  -I/usr/include/mysql -I. -I.. -lmysqlclient
gcc -fPIC -shared -g -std=c99 -O0 -o db_sqlite.so plugin_db/plugin_sqlite.c  -I. -I.. $(pkg-config --cflags --libs sqlite3)

mv db_mysql.so ../plugins/db_mysql.so
mv db_sqlite.so ../plugins/db_sqlite.so

mv geod ../bin/geod