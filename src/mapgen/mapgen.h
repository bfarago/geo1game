/*
 * File:    mapgen.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 */
#ifndef MAPGEN_H
#define MAPGEN_H

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

#define MAPGEN_CONFIGVERSION 1

#if MAPGEN_CONFIGVERSION == 1
// 9seconds, more detailed bin than the UI needs, but provides manageable texture sizes, and bin file sizes
// 0.1° step, approx 10x10km at equator
// we can further remove this detailing in the future, if needed. But native c side is much faster than python side. 
#define MAPGEN_RESOLUTION 0.1f
#define MAPGEN_MULTIPLIER 10.0f
#define LAT_POINTS 1800   // -90 to +90, step 0.1°
#define LON_POINTS 3600   // -180 to +180, step 0.1°
#elif MAPGEN_CONFIGVERSION == 2
// 30s, more detailed bin than the UI needs actually
#define MAPGEN_RESOLUTION 0.05f
#define MAPGEN_MULTIPLIER 20.0f
#define LAT_POINTS 3600   // -90 to +90, step 0.1°
#define LON_POINTS 7200   // -180 to +180, step 0.1°
#endif

// Elevation +-1.0f scaled to +-32767 sint16 to store in 2 bytes
#define MAPGEN_ELEV_SCALE (1.0f / 32767.0f)

// error codes
typedef enum  {
    MAPGEN_OK = 0,
    MAPGEN_ERR = -1,
    MAPGEN_ERR_NO_MEM = -2,
    MAPGEN_ERR_INVALID_PARAM = -3,
} mapgen_ret;

// terrain flags
#define FLAG_COLD       0x01
#define FLAG_UNDERWATER 0x02

// internal data storage type
typedef struct {
    short elevation;  // 2 byte
    unsigned char r;  // 1 byte
    unsigned char g;  // 1 byte
    unsigned char b;  // 1 byte
    unsigned char precip; // 1 byte
    unsigned char temp;   // 1 byte
    unsigned char flags; // 1 byte
} __attribute__((packed)) __attribute__((aligned(8))) MapPoint;

// interface to python api, using float
typedef struct TerrainInfo {
    float elevation;
    unsigned char r, g, b;
    unsigned char precip, temp;
} TerrainInfo;

// Global variable type to hold the map data
typedef struct {
    size_t mapsize;
    long lonsize;
    MapPoint *mapdata;
    mapgen_ret datastatus;
    mapgen_ret filestatus;
    unsigned char need_update;
} Map;

/** Init the mapgen module
* This function initializes the mapgen module, allocating memory for the map data and loading existing data if available.
*/
mapgen_ret mapgen_init(void);

/** Clean the mapgen storage file
* This function deletes the mapgen storage file if it exists, allowing for a fresh start.
*/
void mapgen_clean(void);

/** Finish the mapgen module
* This function cleans up the mapgen module, freeing allocated memory and resetting the map data.
* In case of memory version was modified during the runtime, it will be saved to the file.
*/
void mapgen_finish(void);

/** Save the mapgen data to a file
 * This function writes the current map data to a file, ensuring that any changes made are saved.
 * This is called automatically when the mapgen module is finished.
 */
void mapgen_flush(void);

/** Overwrite one point in the map
 * This function sets the color and elevation of a specific point in the map.
 */
mapgen_ret mapgen_set_point(float lat, float lon, float elevation,
     unsigned char r, unsigned char g, unsigned char b,
     unsigned char precip, unsigned char temp);

/** Get one point from the map
 * This function retrieves the color and elevation of a specific point in the map.
 */
TerrainInfo mapgen_get_terrain_info(float lat, float lon);

#endif // MAPGEN_H
