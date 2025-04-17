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
#define MAPGEN_RESOLUTION 0.1f
#define MAPGEN_MULTIPLIER 10.0f
#define LAT_POINTS 1800   // -90 to +90, step 0.1°
#define LON_POINTS 3600   // -180 to +180, step 0.1°
#elif MAPGEN_CONFIGVERSION == 2
#define MAPGEN_RESOLUTION 0.05f
#define MAPGEN_MULTIPLIER 20.0f
#define LAT_POINTS 3600   // -90 to +90, step 0.1°
#define LON_POINTS 7200   // -180 to +180, step 0.1°
#endif




#define MAPGEN_ELEV_SCALE (1.0f / 32767.0f)

typedef enum  {
    MAPGEN_OK = 0,
    MAPGEN_ERR = -1,
    MAPGEN_ERR_NO_MEM = -2,
    MAPGEN_ERR_INVALID_PARAM = -3,
} mapgen_ret;

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
typedef struct {
    float elevation;
    unsigned char r, g, b;
    unsigned char precip, temp;
} TerrainInfo;

typedef struct {
    long mapsize;
    long lonsize;
    MapPoint *mapdata;
    mapgen_ret datastatus;
    mapgen_ret filestatus;
    unsigned char need_update;
} Map;




mapgen_ret mapgen_init(void);
void mapgen_cleanup(void);
void mapgen_flush(void);
mapgen_ret mapgen_set_point(float lat, float lon, float elevation,
     unsigned char r, unsigned char g, unsigned char b,
     unsigned char precip, unsigned char temp);
TerrainInfo mapgen_get_terrain_info(float lat, float lon);

#endif // MAPGEN_H