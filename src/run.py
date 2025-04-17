#!/usr/bin/env python
import libmapgen
from PIL import Image
import os
import sqlite3
import random
import time

start_time = time.time()
# database path
db_path = "../var/mapdata.sqlite"
# Output directory for images
output_dir = "../www"

print "GEO New world generation started."
# Initialize the map generator module
libmapgen.init()
#libmapgen.generate()
#libmapgen.flush()
print("Elapsed time: %.3f seconds" % (time.time() - start_time))

# Image resolution based on map step
step = 0.1

width = int(360 / step)
height = int(180 / step)

# Database resolution
stepdb= 0.5
widthdb = int(360 / stepdb)
heightdb = int(180 / stepdb)

print "Delete old database."

if os.path.exists(db_path):
    os.remove(db_path)

conn = sqlite3.connect(db_path)
c = conn.cursor()
try:
    c.execute("DROP TABLE IF EXISTS regions")
    c.execute("DROP TABLE IF EXISTS mapdata")
    c.execute('''
    CREATE TABLE mapdata (
        lat REAL,
        lon REAL,
        elevation REAL,
        r REAL,
        g REAL,
        b REAL,
        PRIMARY KEY(lat, lon)
    )
    ''')

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

biome_img = Image.new("RGB", (width, height))
elev_img = Image.new("L", (width, height))
cloud_img = Image.new("RGBA", (width, height))
print "Create textures in memory"

# Iterate over the globe grid and fetch data from C module
for y in range(height):
    lat = 90.0 - y * step
    for x in range(width):
        lon = -180.0 + x * step
        info = libmapgen.get_terrain_info(lat, lon)

        biome_img.putpixel((x, y), (info.r, info.g, info.b))

#        grey = int((info.elevation + 1.0) * 127.5)
        grey = int(info.elevation * 255.0)
        grey = max(0, min(255, grey))
        elev_img.putpixel((x, y), grey)

        prec = int(info.precipitation + 1.0)
        cloud_img.putpixel((x, y), (255, 255, 255, prec))

if not os.path.exists(output_dir):
    os.makedirs(output_dir)
print "Write textures to disk"

biome_img.save(os.path.join(output_dir, "biome.png"))
elev_img.save(os.path.join(output_dir, "elevation.png"))
cloud_img.save(os.path.join(output_dir, "clouds.png"))
print "Collect livable regions"
for y in range(heightdb):
    lat = 90.0 - y * stepdb
    for x in range(widthdb):
        lon = -180.0 + x * stepdb
        info = libmapgen.get_terrain_info(lat, lon)
        biom=""
        c.execute("INSERT INTO mapdata (lat, lon, elevation, r, g, b) VALUES (?, ?, ?, ?, ?, ?)",
                  (lat, lon, info.elevation, info.r, info.g, info.b))
conn.commit()
    
print "Inserted mapdata into 'mapdata' table."

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

    selected = random.sample(candidates, 2000)

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

# Clean up the module
libmapgen.cleanup()
conn.close()

end_time = time.time()
elapsed = end_time - start_time

print("Elapsed time: %.3f seconds" % elapsed)
print "GEO New world generation finished."