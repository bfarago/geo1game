# libmapgen.py
import ctypes
from ctypes import c_float, c_ubyte, Structure

class TerrainInfo(Structure):
    _fields_ = [
        ('elevation', c_float),
        ('r', c_ubyte),
        ('g', c_ubyte),
        ('b', c_ubyte),
        ('precipitation', c_ubyte),
        ('temperature', c_ubyte),
    ]

lib = ctypes.CDLL('./libmapgen_c.so')

lib.mapgen_init.restype = ctypes.c_int
lib.mapgen_cleanup.restype = None
lib.mapgen_get_terrain_info.argtypes = [c_float, c_float]
lib.mapgen_get_terrain_info.restype = TerrainInfo
lib.mapgen_set_point.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
lib.mapgen_set_point.restype = ctypes.c_int

def init():
    result = lib.mapgen_init()
    if result != 0:
        raise RuntimeError("Initialization of the mapgen modul was not successful. (%d) Abort."%(result))
    return result

def get_terrain_info(lat, lon):
    return lib.mapgen_get_terrain_info(lat, lon)

def cleanup():
    lib.mapgen_cleanup()

def set_point(lat, lon, r, g, b, precipitation, temperature):
    return lib.mapgen_set_point(ctypes.c_float(lat), ctypes.c_float(lon),
                                ctypes.c_ubyte(r), ctypes.c_ubyte(g), ctypes.c_ubyte(b),
                                ctypes.c_ubyte(precipitation), ctypes.c_ubyte(temperature)) 

def generate():
    return lib.mapgen_generate()

def flush():
    return lib.mapgen_flush()