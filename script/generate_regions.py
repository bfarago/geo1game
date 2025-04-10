#!/usr/bin/env python
"""Generator script for region objects into the SQL.
regions table row contains one cell of a region, which could
actually shown on the map, at night it could glow, at day it is a city like
object.
Later it could have resources, economy agents, etc.
"""
import sqlite3
import os
import random

__author__ = "Barna Farago"
__copyright__ = "Copyright 2025, MYND-ideal ltd."
__credits__ = ["Barna Faragot"]
__license__ = "GPL"
__version__ = "1.0.1"
__maintainer__ = "Barna Farago"
__email__ = "bfarago@gmail.com"
__status__ = "Development"

db_path = "../var/mapdata.sqlite"

conn = sqlite3.connect(db_path)
c = conn.cursor()

try:
    c.execute("DROP TABLE IF EXISTS regions")
    c.execute("""
        CREATE TABLE regions (
            id INTEGER PRIMARY KEY,
            lat REAL,
            lon REAL,
            lat2 REAL,
            lon2 REAL,
            elevation REAL,
            population REAL,
            light_pollution REAL,
            name TEXT
        )
    """)
except Exception as e:
    print "Error during table creation:", e
    conn.close()
    exit(1)

prefixes = ['Ark', 'Bel', 'Dor', 'Fen', 'Gor', 'Kal', 'Lor', 'Mar', 'Nor', 'Sel', 'Tor', 'Vas', 'Zar',
            'Al', 'Bra', 'Cam', 'Del', 'Er', 'Fal', 'Gal', 'Hel', 'Ith', 'Jar', 'Kel', 'Len', 'Mor',
            'Nol', 'Or', 'Pra', 'Quel', 'Ral', 'Ser', 'Tal', 'Ul', 'Val', 'Wen', 'Xan', 'Yor', 'Zel']

mids = ['an', 'ar', 'en', 'el', 'or', 'ol', 'ir', 'il', 'un', 'ur', 'eth', 'im', 'is', 'ith', 'est', 'os', 'oth']

suffixes = ['dale', 'grad', 'heim', 'land', 'mere', 'port', 'ridge', 'shire', 'ton', 'vale', 'ville', 'wald',
            'keep', 'watch', 'hold', 'hollow', 'cairn', 'fell', 'march', 'moor', 'gate', 'forge', 'reach']

used_names = set();
try:
    c.execute("""
     SELECT lat, lon, elevation
     FROM mapdata
     WHERE elevation > 0
      AND elevation < 0.7
      AND lat > -60
      AND lat < 60
    """)

    candidates = c.fetchall()
    print "Total land points:", len(candidates)

    selected = random.sample(candidates, 5000)

    for lat, lon, elevation in selected:
        lat = round(lat, 2)
        lon = round(lon, 2)
        lat2 = lat + 0.5
        lon2 = lon + 0.5
        population = round(random.uniform(0.1, 1.0), 3)
        pollution = round(random.uniform(0.1, 1.0), 3)
        while True:
            name = random.choice(prefixes) + random.choice(mids) + random.choice(suffixes)
            if name not in used_names:
                used_names.add(name)
                break
        c.execute("INSERT INTO regions (lat, lon, lat2, lon2, elevation, population, light_pollution, name) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                  (lat, lon, lat2, lon2, elevation,population, pollution, name))

    conn.commit()
    print "Inserted regions into 'regions' table."

except Exception as e:
    print "Error during insert:", e

conn.close()
