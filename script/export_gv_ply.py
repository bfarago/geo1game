#!/usr/bin/env python
"""Exports the globe surface, only the vectors.
"""
import sqlite3
import math

__author__ = "Barna Farago"
__copyright__ = "Copyright 2025, MYND-ideal ltd."
__credits__ = ["Barna Faragot"]
__license__ = "GPL"
__version__ = "1.0.1"
__maintainer__ = "Barna Farago"
__email__ = "bfarago@gmail.com"
__status__ = "Development"

db_path = "../var/mapdata.sqlite"
ply_file = "../www/planetvectors_export.ply"

conn = sqlite3.connect(db_path)
c = conn.cursor()
c.execute("SELECT lat, lon, elevation, r, g, b FROM mapdata")

vertices = []
for row in c.fetchall():
    lat, lon, elev, r, g, b = row
    lat_rad = lat * 3.141592 / 180.0
    lon_rad = lon * 3.141592 / 180.0
    radius = 1 + elev * 0.02
    x = radius * math.cos(lat_rad) * math.cos(lon_rad)
    y = radius * math.sin(lat_rad)
    z = radius * math.cos(lat_rad) * math.sin(lon_rad)
    vertices.append((x, y, z, int(r * 255), int(g * 255), int(b * 255)))

with open(ply_file, 'w') as f:
    f.write("ply\n")
    f.write("format ascii 1.0\n")
    f.write("element vertex %d\n" % len(vertices))
    f.write("property float x\n")
    f.write("property float y\n")
    f.write("property float z\n")
    f.write("property uchar red\n")
    f.write("property uchar green\n")
    f.write("property uchar blue\n")
    f.write("element face 0\n")
    f.write("property list uchar int vertex_index\n")
    f.write("end_header\n")
    for v in vertices:
        f.write("%f %f %f %d %d %d\n" % v)

print("done.")

