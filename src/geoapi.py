#!/usr/bin/env python
"""
File:       run.py
Author:     Barna Farago MYND-ideal ltd.
Created:    2025-04-10

Description:
 Preliminary web service for retrieving regions from a database.
"""
import socket
import sqlite3
import json
import time
import threading
import Queue
import logging
import libmapgen
import os
import gc
from PIL import Image

LOGFILE = "../var/geoapi.log"
DB_PATH = "../var/mapdata.sqlite"
CACHE_DIR = "../var/"

# Output directory for images
output_dir = "../www/m"
# http server address
HOST = "127.0.0.1"
#HOST = "0.0.0.0"
PORT = 8008
NUM_WORKERS = 5
IDLE_TIMEOUT = 10  # seconds

logging.basicConfig(
    filename=LOGFILE,
    level=logging.INFO,
    format="%(asctime)s %(threadName)s %(clientip)s %(status)s %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S"
)

task_queue = Queue.Queue()
workers_last_activity = time.time()
workers_map_activity = [0] * NUM_WORKERS
workers_sqlite_activity = [0] * NUM_WORKERS
shutdown_event = threading.Event()
map_loaded = False
sqlite_loaded = False
db_conns= [None] * NUM_WORKERS   # list of sqlite3 connections for each worker

def map_usage_start(worker_id):
    global map_loaded, workers_map_activity, workers_last_activity
    if not map_loaded:
        res=libmapgen.init()
        log_system("Map loaded: %d" % (res))
        if res != 0:
            raise RuntimeError("Initialization of the mapgen modul was not successful. (%d) Abort." % (res))
        map_loaded = True
    workers_map_activity[worker_id] = 1
    workers_last_activity = time.time()
def map_usage_stop(worker_id):
    global workers_map_activity
    workers_map_activity[worker_id] = 0

def sqlite_usage_start(worker_id):
    global sqlite_loaded, db_conns, workers_last_activity, workers_sqlite_activity
    if db_conns[worker_id] is None:
        db_conns[worker_id] = sqlite3.connect(DB_PATH)
        sqlite_loaded = True
        log_system("SQLite loaded for worker %d" % (worker_id))
    workers_sqlite_activity[worker_id] = 1
    workers_last_activity = time.time()
    return db_conns[worker_id]
def sqlite_usage_stop(worker_id):
    global workers_sqlite_activity
    workers_sqlite_activity[worker_id] = 0

def worker_thread(worker_id):
    while True:
        task = task_queue.get()
        if task is None:
            task_queue.task_done()
            break
         # Special command handling
        if isinstance(task, str):
            if task == "__close_sqlite__":
                if db_conns[worker_id] is not None:
                    try:
                        db_conns[worker_id].close()
                        db_conns[worker_id] = None
                        log_system("SQLite connection closed by worker %d" % worker_id)
                    except Exception as e:
                        log_system("SQLite close failed in worker %d: %s" % (worker_id, e))
                task_queue.task_done()
                continue
        conn, addr = task
        try:
            handle_request(conn, addr, worker_id)
        except Exception as e:
            log_system("Error: %s"%(e))
        finally:
            conn.close()
            task_queue.task_done()
def housekeeper_thread():
    global map_loaded
    global sqlite_loaded
    global workers_last_activity
    global workers_map_activity
    global workers_sqlite_activity
    global db_conns
    global shutdown_event
    while not shutdown_event.is_set():
        now = time.time()
        if now - workers_last_activity > IDLE_TIMEOUT :
            if map_loaded:
                if not any(workers_map_activity):
                    log_system("Idle timeout reached. Unloading mapgen.")
                    libmapgen.finish()
                    map_loaded = False
            if sqlite_loaded:
                if not any(workers_sqlite_activity):
                    for i in range(NUM_WORKERS):
                        if db_conns[i] is not None and workers_sqlite_activity[i] == 0:
                            task_queue.put("__close_sqlite__")
                    if all(conn is None for conn in db_conns):
                        sqlite_loaded = False
            if now - workers_last_activity > IDLE_TIMEOUT * 2:
                #if not map_loaded and not sqlite_loaded:
                gc.collect()
                log_system("GC triggered after all resources released.")
        for _ in range(10):
            if shutdown_event.is_set():
                return
            time.sleep(1)
def log_request(addr, status, line):
    extra = {'clientip': addr[0], 'status': status}
    logging.info(line.strip(), extra=extra)

def log_system(line):
    extra = {"clientip": "-", "status": "INFO"}
    logging.info(line.strip(), extra=extra)

def parse_query_string(query):
    params = {}
    pairs = query.split("&")
    for pair in pairs:
        if "=" in pair:
            key, value = pair.split("=", 1)
            params[key] = value
    return params
def extract_ip(sockaddr, headers):
    ip = sockaddr
    for line in headers:
        if line.lower().startswith("x-forwarded-for:"):
            try:
                ip = line.split(":", 1)[1].strip().split(",")
            except:
                pass
            break
    return ip
def get_lat_lon_bounds(query):
    lat_min = float(query.get("lat_min", "-90"))
    lat_max = float(query.get("lat_max", "90"))
    lon_min = float(query.get("lon_min", "-180"))
    lon_max = float(query.get("lon_max", "180"))
    lat_min = max(-90.0, min(90.0, lat_min))
    lat_max = max(-90.0, min(90.0, lat_max))
    lon_min = max(-180.0, min(180.0, lon_min))
    lon_max = max(-180.0, min(180.0, lon_max))
    if lat_min > lat_max:
        lat_min, lat_max = lat_max, lat_min
    if lon_min > lon_max:
        lon_min, lon_max = lon_max, lon_min
    return lat_min, lat_max, lon_min, lon_max

def send_json_response(conn, body):
    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s" % (len(body), body)
    conn.sendall(response.encode("utf-8"))
def send_png_response(conn, image_path):
    with open(image_path, "rb") as f:
        data = f.read()
    headers = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(data)
    conn.sendall(headers.encode("utf-8") + data)

def generate_biome_image(lat_min, lat_max, lon_min, lon_max, width, height, output_path):
    img = Image.new("RGB", (width, height))
    step_lat = (lat_max - lat_min) / float(height)
    step_lon = (lon_max - lon_min) / float(width)
    for y in range(height):
        lat = lat_min + y * step_lat
        for x in range(width):
            lon = lon_min + x * step_lon
            info = libmapgen.get_terrain_info(lat, lon)
            color = (int(info.r), int(info.g), int(info.b))
            img.putpixel((x, y), color)
    img.save(output_path, "PNG")
    img.close()
    del img

def generate_clouds_image(lat_min, lat_max, lon_min, lon_max, width, height, output_path):
    img = Image.new("RGBA", (width, height))
    step_lat = (lat_max - lat_min) / float(height)
    step_lon = (lon_max - lon_min) / float(width)
    min_precipitation = 255
    max_precipitation = 0
    for y in range(height):
        lat = lat_min + y * step_lat
        for x in range(width):
            lon = lon_min + x * step_lon
            info = libmapgen.get_terrain_info(lat, lon)
            g = int(info.precipitation)
            if (g < min_precipitation):
                min_precipitation = g
            if (g > max_precipitation):
                max_precipitation = g
    for y in range(height):
        lat = lat_min + y * step_lat
        for x in range(width):
            lon = lon_min + x * step_lon
            info = libmapgen.get_terrain_info(lat, lon)
            g = (info.precipitation-min_precipitation)*255 / max_precipitation
            img.putpixel((x, y), (255, 255, 255, g))
    img.save(output_path, "PNG")
    img.close()
    del img

def generate_elevation_image(lat_min, lat_max, lon_min, lon_max, width, height, output_path):
    img = Image.new("L", (width, height))
    step_lat = (lat_max - lat_min) / float(height)
    step_lon = (lon_max - lon_min) / float(width)
    for y in range(height):
        lat = lat_min + y * step_lat
        for x in range(width):
            lon = lon_min + x * step_lon
            info = libmapgen.get_terrain_info(lat, lon)
            g = max(0, min(255, int(info.elevation * 255)))
            img.putpixel((x, y), g)
    img.save(output_path, "PNG")
    img.close()
    del img

def handle_request(conn, sockaddr, worker_id):
    request = conn.recv(4096)
    global workers_last_activity
    if not request:
        return

    try:
        request = request.decode("utf-8")
    except:
        pass  # ignore invalid requests
    
    lines = request.split("\r\n")
    addr = extract_ip(sockaddr, lines)
    request_line = lines[0]
    try:
        method, full_path, _ = request_line.split(" ")
    except:
        log_request(addr, "400", request_line)
        conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
        return
    path = full_path
    query_string = ""
    if "?" in full_path:
        path, query_string = full_path.split("?", 1)
    query = parse_query_string(query_string)
    start_time = time.time()
    workers_last_activity = start_time
    if method == "GET" and path == "/regions_chunk":
        db_conn= sqlite_usage_start(worker_id)
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        cur = db_conn.cursor()
        cur.execute("""
            SELECT lat, lon, elevation, light_pollution, name
            FROM regions
            WHERE lat >= ? AND lat < ?
              AND lon >= ? AND lon < ?
        """, (lat_min, lat_max, lon_min, lon_max))
        rows = cur.fetchall()
        cur.close()
        sqlite_usage_stop(worker_id)
        result = {}
        for row in rows:
            lat, lon, elev, light, name = row
            key = "%.2f,%.2f" % (round(lat, 2), round(lon, 2))
            result[key] = {
                "r": 1.0,
                "g": 1.0,
                "b": 1.0,
                "e": elev,
                "name": name
            }
        duration = int((time.time() - start_time) * 1000)
        body = json.dumps(result)
        response = (
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n%s" % (len(body), body)
        )
        send_json_response(conn, body)
        log_request(addr, "200", request_line+ " [%d ms]" % duration)
    elif method == "GET" and path == "/mapdata":
        db_conn= sqlite_usage_start(worker_id)
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        cur = db_conn.cursor()
        cur.execute("""
            SELECT lat, lon, r, g, b, elevation
            FROM mapdata
            WHERE lat BETWEEN ? AND ? AND lon BETWEEN ? AND ?
            LIMIT 50000
        """, (lat_min, lat_max, lon_min, lon_max))
        rows = cur.fetchall()
        cur.close()
        sqlite_usage_stop(worker_id)
        result = {}
        for row in rows:
            lat, lon, r, g, b, elev = row
            key = "%.2f,%.2f" % (round(lat, 2), round(lon, 2))
            result[key] = {"r": int(r), "g": int(g), "b": int(b), "e": elev}
        duration = int((time.time() - start_time) * 1000)
        body = json.dumps(result)
        send_json_response(conn, body)
        log_request(addr, "200", request_line+ " [%d ms]" % duration)
    elif method == "GET" and path == "/map":
        map_usage_start(worker_id)
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        try:
            step = float(query.get("step", "0.5"))
        except:
            step = 0.5
        if step <= 0 or lat_min >= lat_max or lon_min >= lon_max:
            conn.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            log_request(addr, "400", request_line)
            return
        point_count = ((lat_max - lat_min) / step) * ((lon_max - lon_min) / step)
        if point_count > 50000:
            conn.sendall(b"HTTP/1.1 413 Request Entity Too Large\r\nContent-Length: 0\r\n\r\n")
            log_request(addr, "413", request_line)
            return
        result = {}
        lat = lat_min
        while lat <= lat_max:
            lon = lon_min
            while lon <= lon_max:
                info = libmapgen.get_terrain_info(lat, lon)
                key = "%.2f,%.2f" % (round(lat, 2), round(lon, 2))
                result[key] = {"r": info.r, "g": info.g, "b": info.b, "e": info.elevation}
                lon += step
            lat += step
        map_usage_stop(worker_id)
        duration = int((time.time() - start_time) * 1000)
        body = json.dumps(result)
        send_json_response(conn, body)
        log_request(addr, "200", request_line+ " [%d ms]" % duration)
    elif method == "GET" and path == "/biome":
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        width = int(query.get("width", "256"))
        height = int(query.get("height", "256"))
        cache_file = os.path.join(CACHE_DIR, "biome_lat%.2f_lon%.2f_%dx%d.png" % (lat_min, lon_min, width, height))
        if not os.path.exists(cache_file):
            map_usage_start(worker_id)
            generate_biome_image(lat_min, lat_max, lon_min, lon_max, width, height, cache_file)
            map_usage_stop(worker_id)
        send_png_response(conn, cache_file)
        duration = int((time.time() - start_time) * 1000)
        log_request(addr, "200", request_line +  " [%d ms]" % duration)
    elif method == "GET" and path == "/clouds":
        
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        width = int(query.get("width", "256"))
        height = int(query.get("height", "256"))
        cache_file = os.path.join(CACHE_DIR, "clouds_lat%.2f_lon%.2f_%dx%d.png" % (lat_min, lon_min, width, height))
        if not os.path.exists(cache_file):
            map_usage_start(worker_id)
            generate_clouds_image(lat_min, lat_max, lon_min, lon_max, width, height, cache_file)
            map_usage_stop(worker_id)
        send_png_response(conn, cache_file)
        duration = int((time.time() - start_time) * 1000)
        log_request(addr, "200", request_line +  " [%d ms]" % duration)
    elif method == "GET" and path == "/elevation":
        lat_min, lat_max, lon_min, lon_max = get_lat_lon_bounds(query)
        width = int(query.get("width", "256"))
        height = int(query.get("height", "256"))
        cache_file = os.path.join(CACHE_DIR, "elevation_lat%.2f_lon%.2f_%dx%d.png" % (lat_min, lon_min, width, height))
        if not os.path.exists(cache_file):
            map_usage_start(worker_id)
            generate_elevation_image(lat_min, lat_max, lon_min, lon_max, width, height, cache_file)
            map_usage_stop(worker_id)
        send_png_response(conn, cache_file)
        duration = int((time.time() - start_time) * 1000)
        log_request(addr, "200", request_line +  " [%d ms]" % duration)
    else:
        log_request(addr, "404", request_line)
        conn.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
def run():
    # start workers
    workers = [] 
    for i in range(NUM_WORKERS):
        t = threading.Thread(target=worker_thread, args=(i,))
        t.daemon = True
        t.start()
        workers.append(t)
    hk = threading.Thread(target=housekeeper_thread)
    hk.start()
    # listen socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen(5)
    log_system("GeoAPI server listening on http://%s:%d/" % (HOST, PORT))

    try:
        while True:
            conn, addr = s.accept()
            task_queue.put((conn, addr))
    except KeyboardInterrupt:
        log_system("\nShutting down server...")
    shutdown_event.set()
    # cleanup
    s.close()
    for _ in workers:
        task_queue.put(None)  # signal to workers to exit
    for t in workers:
        t.join()
    hk.join(timeout=1)
    log_system("Server shutdown complete.")
if __name__ == "__main__":
    run()