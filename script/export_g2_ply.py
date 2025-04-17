import sqlite3
import math

db_path = "../var/mapdata.sqlite"
ply_file = "../www/planet_export.ply"

conn = sqlite3.connect(db_path)
c = conn.cursor()

lat_min = -90
lat_max = 90
lon_min = -180
lon_max = 180
step = 1.0
lat_steps = int((lat_max - lat_min) / step) + 1
lon_steps = int((lon_max - lon_min) / step) + 1

vertices = []
index_map = {}
faces = []

c.execute("SELECT lat, lon, elevation, r, g, b FROM mapdata")
rows = c.fetchall()

for row in rows:
    lat, lon, elev, r, g, b = row
    lat_rad = lat * math.pi / 180.0
    lon_rad = lon * math.pi / 180.0
    radius = 1.0 + elev * 0.02
    x = radius * math.cos(lat_rad) * math.cos(lon_rad)
    y = radius * math.sin(lat_rad)
    z = radius * math.cos(lat_rad) * math.sin(lon_rad)
    idx = len(vertices)
    vertices.append((x, y, z, int(r * 255), int(g * 255), int(b * 255)))
    index_map["%.2f,%.2f" % (lat, lon)] = idx

for lat_idx in range(lat_min, lat_max, int(step)):
    for lon_idx in range(lon_min, lon_max, int(step)):
        key1 = "%.2f,%.2f" % (lat_idx, lon_idx)
        key2 = "%.2f,%.2f" % (lat_idx + step, lon_idx)
        key3 = "%.2f,%.2f" % (lat_idx + step, lon_idx + step)
        key4 = "%.2f,%.2f" % (lat_idx, lon_idx + step)
        if key1 in index_map and key2 in index_map and key3 in index_map:
            faces.append((index_map[key1], index_map[key2], index_map[key3]))
        if key1 in index_map and key3 in index_map and key4 in index_map:
            faces.append((index_map[key1], index_map[key3], index_map[key4]))

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
    f.write("element face %d\n" % len(faces))
    f.write("property list uchar int vertex_index\n")
    f.write("end_header\n")
    for v in vertices:
        f.write("%f %f %f %d %d %d\n" % v)
    for face in faces:
        f.write("3 %d %d %d\n" % face)

print("done.")
