#!/bin/sh
# * * * * * /home/brown/src/geo/watchdog.sh
#
GEOD_PIDFILE="/home/brown/src/geo/var/geod.pid"
GEOD_SERVICE="/etc/init.d/geod"
STATUS_URL="http://127.0.0.1:8008/status.json"
SERVICE="/etc/init.d/geod"
TIMEOUT=3

RESPONSE=$(curl -s --max-time $TIMEOUT "$STATUS_URL")

STATUS=$(echo "$RESPONSE" | grep -o '"status":"running"')
if [ "$STATUS" != '"status":"running"' ]; then
    logger "geod watchdog: daemon not healthy, restarting..."
    rm -f $GEOD_PIDFILE
    $SERVICE restart
fi