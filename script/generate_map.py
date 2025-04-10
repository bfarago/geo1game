#!/usr/bin/env python
"""Generator script for the globe rough sourface into the SQL.
Very first implementation, simplistic, it is using only a 3d Perlin noise algorithm.
Additionally handles the North and South poles like a colder zone.
mapdata table row contains elevation, color, biome, and lat/lon coordinates.
"""
from perlin import SimplexNoise
import sqlite3
import os
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
if os.path.exists(db_path):
    os.remove(db_path)

conn = sqlite3.connect(db_path)
c = conn.cursor()
c.execute("DROP TABLE IF EXISTS regions")
c.execute('''
CREATE TABLE mapdata (
    lat REAL,
    lon REAL,
    elevation REAL,
    biome TEXT,
    r REAL,
    g REAL,
    b REAL,
    PRIMARY KEY(lat, lon)
)
''')
noise = SimplexNoise()
def perlin_on_sphere(lat, lon, scale=5.0, repeat=1024):
    phi = math.radians(90.0 - lat)
    theta = math.radians(lon + 180.0)
    x = math.sin(phi) * math.cos(theta)
    y = math.cos(phi)
    z = math.sin(phi) * math.sin(theta)
    return noise.noise3(x * scale, y * scale, z * scale)
    # repeatx=repeat, repeaty=repeat, repeatz=repeat)
def elevation_to_biome_and_color(e, lat):
    if e < -0.05:
        return "ocean", (0.0, 0.4, 0.8)
    elif e < 0.0:
        return "shore", (0.9, 0.8, 0.6)
    elif e < 0.4:
        if abs(lat)>70:
            return "iceland", (0.9, 0.9, 0.9)
        else:
            return "grassland", (0.1, 0.7, 0.2)
    elif e < 0.88:
        if abs(lat)>70:
            return "iceland", (0.7, 0.7, 0.9)
        else:
            return "mountain", (0.5, 0.5, 0.5)
    else:
        return "icecup", (0.9, 0.9, 0.9)

scale = 0.04
lat = -90.0
while lat <= 90.0:
    lon = -180.0
    while lon <= 180.0:
	e = perlin_on_sphere(lat, lon, scale=2.0)
        biome, (r, g, b) = elevation_to_biome_and_color(e, lat)
        c.execute('INSERT INTO mapdata VALUES (?, ?, ?, ?, ?, ?, ?)', (lat, lon, e, biome, r, g, b))
        lon += 0.5
    lat += 0.5
conn.commit()
conn.close()
