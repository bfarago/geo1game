"""
File:       libmapgen.py
Author:     Barna Farago MYND-ideal ltd.
Created:    2025-04-10

Description:
    Python binding for the procedural map generation library (libmapgen_c.so).
    Provides access to low-level C functions for terrain synthesis,
    elevation data processing, and biome classification.

    Key features:
        - ctypes-based interface to compiled C shared library
        - Structured access to MapPoint data
        - Region extraction and coordinate mapping utilities
        - Support for binary elevation file operations

Usage:
    - Call load_library() to initialize and bind the shared object
    - Use get_info(), get_lat_lon(), get_point() for map queries
    - Use generate_map() to trigger C-side terrain generation
    - Use read/write helpers for binary file I/O

Dependencies:
    - ctypes, numpy, struct
    - libmapgen_c.so (compiled C library)

Notes:
    - Assumes compatible binary layout with C MapPoint struct
    - Latitude [-90..90], Longitude [-180..180] range conventions
    - Elevation scaled as signed short (-32768..32767)
"""
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
lib.mapgen_clean.restype = None
lib.mapgen_finish.restype = None
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

def clean():
    lib.mapgen_clean()

def finish():
    lib.mapgen_finish()

def set_point(lat, lon, r, g, b, precipitation, temperature):
    return lib.mapgen_set_point(ctypes.c_float(lat), ctypes.c_float(lon),
                                ctypes.c_ubyte(r), ctypes.c_ubyte(g), ctypes.c_ubyte(b),
                                ctypes.c_ubyte(precipitation), ctypes.c_ubyte(temperature)) 

def generate():
    return lib.mapgen_generate()

def flush():
    return lib.mapgen_flush()