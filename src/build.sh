#!/bin/bash
# This script builds the C extension for the mapgen module.
# It is purposefully simple and does not use any build system.
MODE="$1"
LOG="build.log"
CC=gcc
CFLAGS="-std=c99 -O0 -march=native -ffast-math -funroll-loops -mfma -mavx2 -Wall -Wextra -g -fPIC"
SHARED_FLAGS="-shared"
INCLUDE_FLAGS="-I./"
BIN_DIR="../bin"
PLUGIN_DIR="../plugins"
echo "">$LOG
# Build geod executable
GEOD_SOURCES="data.c data_sql.c data_geo.c"
GEOD_SOURCES="$GEOD_SOURCES config.c http.c cache.c handlers.c sync.c json_indexlist.c pluginhst.c"
GEOD_SOURCES="$GEOD_SOURCES geod.c "
$CC $CFLAGS -o geod $GEOD_SOURCES -lpng -ldl -lpthread -lm -lssl -lcrypto -ljson-c 2>>$LOG

# Build mapgen C extension
$CC -std=c99 -O3 -march=native -ffast-math -funroll-loops -mfma -mavx2 -shared -fPIC -o libmapgen_c.so mapgen/mapgen.c mapgen/perlin3d.c 2>>$LOG

#
# PLUGINS
#

# CGI plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o cgi.so plugin_cgi/plugin_cgi.c 2>>$LOG
# Control plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o control.so plugin_control/plugin_control.c sync.c 2>>$LOG
# WS plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o ws.so plugin_ws/plugin_ws.c plugin_ws/ws.c -lssl -lcrypto -ljson-c 2>>$LOG
# HTTP Hello plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o http_hello.so plugin_http_hello/plugin_http_hello.c 2>>$LOG
# Image plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o image.so plugin_image/plugin_image.c -lpng 2>>$LOG
# Texture plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o texture.so plugin_texture/plugin_texture.c -lm 2>>$LOG
# Localmap plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o localmap.so plugin_texture/plugin_localmap.c -lm 2>>$LOG
# Region plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o region.so plugin_region/plugin_region.c 2>>$LOG
# Shape plugin
$CC $CFLAGS $SHARED_FLAGS $INCLUDE_FLAGS -o shape.so plugin_shape/plugin_shape.c plugin_shape/shape.c plugin_shape/plan.c -lm 2>>$LOG
# DB MySQL plugin
$CC -fPIC -shared -g -std=c99 -O0 -o db_mysql.so sync.c plugin_db/plugin_mysql.c -I/usr/include/mysql -I. -I.. -lmysqlclient 2>>$LOG
# DB SQLite plugin
$CC -fPIC -shared -g -std=c99 -O0 -o db_sqlite.so plugin_db/plugin_sqlite.c -I. -I.. $(pkg-config --cflags --libs sqlite3) 2>>$LOG

# Move compiled binaries to their destination only on 'install'
if [[ "$1" == "install" ]]; then
service geod stop
mv libmapgen_c.so $BIN_DIR/libmapgen_c.so
mv cgi.so $PLUGIN_DIR/cgi.so
mv control.so $PLUGIN_DIR/control.so
mv ws.so $PLUGIN_DIR/ws.so
mv http_hello.so $PLUGIN_DIR/http_hello.so
mv image.so $PLUGIN_DIR/image.so
mv texture.so $PLUGIN_DIR/texture.so
mv localmap.so $PLUGIN_DIR/localmap.so
mv region.so $PLUGIN_DIR/region.so
mv shape.so $PLUGIN_DIR/shape.so
mv db_mysql.so $PLUGIN_DIR/db_mysql.so
mv db_sqlite.so $PLUGIN_DIR/db_sqlite.so
mv geod $BIN_DIR/geod
service geod start
fi

if [[ "$1" == "vscode" ]]; then
    perl -pe 's|/home/brown/src/geo/src/|/|g; s#^([a-zA-Z0-9_\/\.-]+\.(c|h)):#/Users/bfarago/remotesrc/geo/src/$1:#' $LOG
else
    cat build.log
fi
