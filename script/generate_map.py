from perlin import SimplexNoise
import sqlite3
import os
import math
import random

db_path = "../var/mapdata.sqlite"

step = 0.1

if os.path.exists(db_path):
    os.remove(db_path)

conn = sqlite3.connect(db_path)
c = conn.cursor()
c.execute("DROP TABLE IF EXISTS regions")
c.execute("DROP TABLE IF EXISTS mapdata")
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

def is_cold_zone(lat, noise_val=0.0):
    t = (abs(lat) - 68.0) / (75.0 - 68.0)
    t += noise_val * 0.2
    return t > 1.0

def perlin_on_sphere(lat, lon, scale=5.0, repeat=1024):
    phi = math.radians(90.0 - lat)
    theta = math.radians(lon + 180.0)
    x = math.sin(phi) * math.cos(theta)
    y = math.cos(phi)
    z = math.sin(phi) * math.sin(theta)
    return noise.noise3(x * scale, y * scale, z * scale) + noise.noise3(x , y , z)/2.0
    # repeatx=repeat, repeaty=repeat, repeatz=repeat)
def fbm_noise3(x, y, z, noise_func, octaves=6, lacunarity=2.0, gain=0.5):
    value = 0.0
    amplitude = 1.0
    frequency = 1.0
    for _ in range(octaves):
        value += noise_func(x * frequency, y * frequency, z * frequency) * amplitude
        frequency *= lacunarity
        amplitude *= gain
    return value
def fbm_on_sphere(lat, lon, scale=5.0, repeat=1024):
    phi = math.radians(90.0 - lat)
    theta = math.radians(lon + 180.0)
    x = math.sin(phi) * math.cos(theta)
    y = math.cos(phi)
    z = math.sin(phi) * math.sin(theta)
    return fbm_noise3(x, y, z, noise.noise3, octaves=6, lacunarity=2.0, gain=0.5)

def elevation_to_biome_and_color(e, lat, lon):
    cold = is_cold_zone(lat, noise.noise2(lat * 0.1, lon * 0.1))
    if e < -0.05:
        return "ocean", (0.0, 0.4, 0.8)
    elif e < 0.0:
        return "shore", (0.9, 0.8, 0.6)
    elif e < 0.4:
        if cold:
            return "tundra", (0.85, 0.85, 0.85)
        else:
            t = (e + 0.05) / (0.4 + 0.05)
            r = 0.2 - t * 0.1
            g = 0.5 + t * 0.4
            b = 0.2 - t * 0.1
            return "grassland", (r, g, b)
    elif e < 0.88:
        if cold:
            return "iceland", (0.7, 0.7, 0.9)
        else:
            t = (e - 0.4) / (0.48)
            gray = 0.4 + 0.4 * t
            return "mountain", (gray, gray, gray)
    else:
        return "icecap", (0.95, 0.95, 0.95)
def add_variance(r, g, b, amount=0.05):
    return (
        max(0, min(1, r + random.uniform(-amount, amount))),
        max(0, min(1, g + random.uniform(-amount, amount))),
        max(0, min(1, b + random.uniform(-amount, amount))),
    )
scale = 0.04
lat = -90.0
while lat <= 90.0:
    lon = -180.0
    while lon <= 180.0:
	e = fbm_on_sphere(lat, lon, scale=2.0)
        biome, (r, g, b) = elevation_to_biome_and_color(e, lat, lon)
        (rr, gg, bb) = add_variance(r, g, b, amount=0.05)
        c.execute('INSERT INTO mapdata VALUES (?, ?, ?, ?, ?, ?, ?)', (lat, lon, e, biome, rr, gg, bb))
        lon += step
    lat += step
conn.commit()
conn.close()
