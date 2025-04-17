#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sqlite3
try:
    from PIL import Image
except ImportError:
    import Image

stepmul = 10 # 10x resolution 1/n degrees
db_path = "../var/mapdata.sqlite"
out_biome_path = "../www/biome.png"
out_elev_path = "../www/elevation.png"
img_width = 360 * stepmul 
img_height = 180 * stepmul 
biome_img = Image.new("RGB", (img_width, img_height))
elev_img = Image.new("L", (img_width, img_height))  # L = grayscale
conn = sqlite3.connect(db_path)
cursor = conn.cursor()
for row in cursor.execute("SELECT lat, lon, elevation, r, g, b FROM mapdata"):
    lat, lon, elev, r, g, b = row
    x = int(round((lon + 180) * stepmul))
    y = int(round((90 - lat) * stepmul))
    if 0 <= x < img_width and 0 <= y < img_height:
        biome_color = (
            int(r * 255),
            int(g * 255),
            int(b * 255)
        )
        biome_img.putpixel((x, y), biome_color)
        elev_gray = int(max(0.0, min(1.0, elev)) * 255)
        elev_img.putpixel((x, y), elev_gray)
conn.close()
biome_img.save(out_biome_path)
elev_img.save(out_elev_path)
