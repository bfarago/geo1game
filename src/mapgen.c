/*
 * File:    mapgen.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 *
 * Description:
 *   Procedural terrain generation module.
 *   Handles map point initialization, elevation classification,
 *   and biome color assignment with support for vectorized processing.
 *
 *   Key features:
 *     - Noise-based elevation generation (Perlin, FBM)
 *     - SIMD-ready batched processing
 *     - Cloud and temperature simulation per latitude band
 *     - Packed memory layout for cache efficiency
 *
 * Usage:
 *   - Call mapgen_generate() to produce a full map in memory.
 *   - Output is stored in g_map.mapdata[] with MapPoint structures.
 *
 * Dependencies:
 *   - perlin3d.c / perlin3d.h : noise functions
 *   - math.h, stdlib.h
 *
 * Notes:
 *   - Coordinate system: latitude [-90..90], longitude [-180..180]
 *   - Elevation: normalized [-1.0 .. 1.0] internally, stored as signed short
 *   - Color classification based on configurable elevation thresholds
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "mapgen.h"
#include "perlin3d.h"

#define MAPGEN_FILENAME "mapdata.bin"


Map  g_map;
static inline unsigned long mapgen_get_index(float lat, float lon) {
    return (lon + 180.0f) * MAPGEN_MULTIPLIER + (lat + 90.0f) * MAPGEN_MULTIPLIER * LON_POINTS;
}

mapgen_ret mapgen_init(void){
    mapgen_ret ret = MAPGEN_OK;
    g_map.mapsize = LAT_POINTS*LON_POINTS;
    g_map.lonsize = LON_POINTS;
    g_map.filestatus = MAPGEN_ERR_NO_MEM;
    g_map.datastatus = MAPGEN_ERR_NO_MEM;
    g_map.need_update = 0;
    init_perlin();
    g_map.mapdata = (MapPoint*)malloc(g_map.mapsize * sizeof(MapPoint));
    if (g_map.mapdata == NULL) {
        ret = MAPGEN_ERR_NO_MEM;
    }else{
        g_map.datastatus = MAPGEN_OK;
        FILE *fp = fopen(MAPGEN_FILENAME, "rb");
        if (fp == NULL) {
            ret = MAPGEN_OK; // File not found, generate new map
        }else {
            size_t read_size = fread(g_map.mapdata, sizeof(MapPoint), g_map.mapsize, fp);
            if (read_size != g_map.mapsize) {
                ret = MAPGEN_ERR_INVALID_PARAM;
            }else{
                g_map.filestatus = MAPGEN_OK;
            }
            fclose(fp);
        }
    }
    return ret;
}

void mapgen_flush(void){
    FILE *fp = fopen(MAPGEN_FILENAME, "wb");
    if (fp != NULL) {
        fwrite(g_map.mapdata, sizeof(MapPoint), g_map.mapsize, fp);
        fclose(fp);
        g_map.filestatus = MAPGEN_OK;
        g_map.need_update = 0;
    }
}

void mapgen_cleanup(void){
    if (g_map.mapdata != NULL) {
        if (g_map.datastatus == MAPGEN_OK) {
            if (g_map.need_update) {
                mapgen_flush();
            }
        }
        free(g_map.mapdata);
        g_map.mapdata = NULL;
    }
    g_map.mapsize = 0;
    g_map.lonsize = 0;
    g_map.datastatus = MAPGEN_ERR_NO_MEM;
    g_map.filestatus = MAPGEN_ERR_NO_MEM;
}
static inline void mapgen_set_point_index(unsigned long index, float elevation, unsigned char r, unsigned char g, unsigned char b, unsigned char precip, unsigned char temp) {
    MapPoint *point = &g_map.mapdata[index];
    point->elevation = elevation * MAPGEN_ELEV_SCALE;
    point->r = r;
    point->g = g;
    point->b = b;
    point->precip = precip;
    point->temp = temp;
}
mapgen_ret mapgen_set_point(float lat, float lon, float elevation, unsigned char r, unsigned char g, unsigned char b, unsigned char precip, unsigned char temp) {
    mapgen_ret  ret= MAPGEN_ERR_INVALID_PARAM;
    unsigned long index = mapgen_get_index(lat, lon);
    if ((g_map.mapdata != NULL) && (index < g_map.mapsize)) {
        mapgen_set_point_index(index, elevation, r, g, b, precip, temp);
        ret= MAPGEN_OK;
        g_map.need_update = 1;
    }
    return ret;
}

TerrainInfo mapgen_get_terrain_info(float lat, float lon) {
    TerrainInfo terrain_info;
    unsigned long index = mapgen_get_index(lat, lon);
    if ((g_map.mapdata == NULL) ||(index >= g_map.mapsize)) {
        terrain_info.elevation = 0;
        terrain_info.r = 0;
        terrain_info.g = 0;
        terrain_info.b = 0;
        terrain_info.precip = 0;
        terrain_info.temp = 0;
        return terrain_info;
    }else {
        MapPoint *point = &g_map.mapdata[index];
        terrain_info.elevation = point->elevation * MAPGEN_ELEV_SCALE;
        terrain_info.r = point->r;
        terrain_info.g = point->g;
        terrain_info.b = point->b;
        terrain_info.precip = point->precip;
        terrain_info.temp = point->temp;
        return terrain_info;
    }
}
float fbm_noise3(float x, float y, float z, float (*noise_func)(float, float, float), int octaves, float lacunarity, float gain) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    int i;
    for (i = 0; i < octaves; i++) {
        value += noise_func(x * frequency, y * frequency, z * frequency) * amplitude;
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

float fbm_on_sphere(float lat, float lon, float scale, float (*noise_func)(float, float, float)) {
    float phi = (float)(M_PI / 180.0 * (90.0 - lat));
    float theta = (float)(M_PI / 180.0 * (lon + 180.0));
    float x = sinf(phi) * cosf(theta);
    float y = cosf(phi);
    float z = sinf(phi) * sinf(theta);
    return fbm_noise3(x * scale, y * scale, z * scale, noise_func, 7, 2.0f, 0.5f);
}

typedef void (*noise_func_n_t)(float* x, float* y, float* z, float* out, int n);

typedef struct {
    //input
    int count;
    //internal
    float *xf;
    float *yf;
    float *zf;
    float *noise;
} WorkFbmNoise3n;

void WorkFbmNoise3n_init(WorkFbmNoise3n* wrk, int count) {
    wrk->count = count;
    wrk->xf = (float*)malloc(sizeof(float) * count);
    wrk->yf = (float*)malloc(sizeof(float) * count);
    wrk->zf = (float*)malloc(sizeof(float) * count);
    wrk->noise = (float*)malloc(sizeof(float) * count); 
    if (wrk->xf == NULL || wrk->yf == NULL || wrk->zf == NULL || wrk->noise == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
}
void WorkFbmNoise3n_free(WorkFbmNoise3n* wrk) {
    free(wrk->xf);
    free(wrk->yf);
    free(wrk->zf);
    free(wrk->noise);
    wrk->count = 0;
}
void WorkFbmNoise3n_compute(
    WorkFbmNoise3n *wrk,
    float* x, float* y, float* z,
    float* out,
    int count,
    noise_func_n_t noise_func,
    int octaves,
    float lacunarity,
    float gain
) {
    memset(out, 0, sizeof(float) * count);
    float frequency = 1.0f;
    float amplitude = 1.0f;
    for (int o = 0; o < octaves; o++) {
        for (int i = 0; i < count; i++) {
            wrk->xf[i] = x[i] * frequency;
            wrk->yf[i] = y[i] * frequency;
            wrk->zf[i] = z[i] * frequency;
        }
        noise_func(wrk->xf, wrk->yf, wrk->zf, wrk->noise, count);
        for (int i = 0; i < count; i++) {
            out[i] += wrk->noise[i] * amplitude;
        }
        frequency *= lacunarity;
        amplitude *= gain;
    }
}

typedef struct {
    //input
    int count;
    //internal
    float *x;
    float *y;
    float *z;
    noise_func_n_t noise_func;
    WorkFbmNoise3n wrkNoise;
} WorkFbmOnSphereN;

void WorkFbmOnSphereN_init(WorkFbmOnSphereN* wrk, int count) {
    wrk->count = count;
    wrk->x = (float*)malloc(sizeof(float) * count);
    wrk->y = (float*)malloc(sizeof(float) * count);
    wrk->z = (float*)malloc(sizeof(float) * count);
    WorkFbmNoise3n_init(&wrk->wrkNoise, count);
}
void WorkFbmOnSphereN_free(WorkFbmOnSphereN* wrk) {
    free(wrk->x);
    free(wrk->y);
    free(wrk->z);
    WorkFbmNoise3n_free(&wrk->wrkNoise);
    wrk->count = 0;
}
void WorkFbmOnSphereN_compute(WorkFbmOnSphereN* wrk, float *lat, float *lon, float scale, float* out, int count, noise_func_n_t noise_func) {
    for (int i = 0; i < count; i++) {
        float phi = (float)(M_PI / 180.0f * (90.0f - lat[i]));
        float theta = (float)(M_PI / 180.0f * (lon[i] + 180.0f));
        float sin_phi = sinf(phi);
        wrk->x[i] = scale * sin_phi * cosf(theta);
        wrk->y[i] = scale * cosf(phi);
        wrk->z[i] = scale * sin_phi * sinf(theta);
    }
    //fbm_noise3n(wrk->x, wrk->y, wrk->z, out, count, noise_func, 7, 2.0f, 0.5f);
    WorkFbmNoise3n_compute(
        &wrk->wrkNoise,
        wrk->x, wrk->y, wrk->z,
        out,
        count,
        noise_func,
        7, 2.0f, 0.5f
    );
}

// Elevation classes
typedef enum {
    MAPGEN_ELEVCLASS_OCEAN = 0,
    MAPGEN_ELEVCLASS_COASTLINE,
    MAPGEN_ELEVCLASS_LOWLAND,
    MAPGEN_ELEVCLASS_HIGHLAND,
    MAPGEN_ELEVCLASS_MOUNTAIN,
    MAPGEN_ELEVCLASS_PEAK,
    MAPGEN_ELEVCLASS_MAX
} ElevClass_en;

typedef struct {
    unsigned char r, g, b;
} ElevClassParam;

float g_elevclass[MAPGEN_ELEVCLASS_MAX] = {
    -0.05f,  // ocean
    0.00f,    // coastline
    0.30f,    // lowland
    0.40f,   // highland
    0.50f,   // mountain
    0.60f    // peak
};

ElevClassParam g_elevclass_param[MAPGEN_ELEVCLASS_MAX] = {
    {0, 102, 204}, // ocean
    {230, 220, 160}, // coastline
    {25, 179, 51}, // lowland
    {12, 128, 12}, // highland
    {128, 128, 128}, // mountain
    {255, 255, 255} // peak
};

ElevClassParam g_elevclass_cold_param[MAPGEN_ELEVCLASS_MAX] = {
    {212, 212, 240}, // ocean
    {230, 220, 160}, // coastline
    {230, 230, 230}, // lowland
    {200, 200, 250}, // highland
    {220, 220, 240}, // mountain
    {255, 255, 255} // peak
};

typedef struct {
    float lat;
    float abslat;
    float elevreduce;
    double lat_ratio;
    float base;
    float jet;
    short edges[MAPGEN_ELEVCLASS_MAX];
} LatData;

static inline void init_lat_data(LatData *pLatData, float lat) {
    pLatData->lat = lat;
    pLatData->abslat = fabs(lat);
    pLatData->lat_ratio = pLatData->abslat / 90.0; // 0..1
    pLatData->base = sinf(3.1415f * pLatData->lat_ratio); // ITCZ + 60° sávos csapadék
    pLatData->jet = fabsf(cosf(pLatData->lat * 3.1415f / 180.0f * 6.0f)); // absz, mert nem érdekel az irány
    pLatData->elevreduce = pLatData->abslat * pLatData->abslat / (90.0*90.0)+0.04;
    for (int i = 0; i < MAPGEN_ELEVCLASS_MAX; i++) {
        float threshold = g_elevclass[i] + pLatData->elevreduce;
        int threshold_i = (int)(threshold * 32767.0f);
        if (threshold_i < -32768) threshold_i = -32768;
        if (threshold_i > 32767) threshold_i = 32767;
        pLatData->edges[i] = (short)(threshold_i);
    }
}
static inline int find_color_index(short *edges, short elev) {
    int lo = 0, hi = MAPGEN_ELEVCLASS_MAX - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (elev <= edges[mid])
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}
static inline void find_color_index_n(short *edges, short *elev, unsigned char *cindexes, int count) {
    for(int i = 0; i < count; i++) {
        cindexes[i] = find_color_index(edges, elev[i]);
    }
}
void unit_short_saturate_n(float *in, short *out, int count) {
    for (int i = 0; i < count; i++) {
        int elev_i32 = (int)(in[i] * 32767.0f);
        if (elev_i32 < -32768) elev_i32 = -32768;
        if (elev_i32 > 32767) elev_i32 = 32767;
        out[i] = (short)elev_i32;
    }
}

typedef struct {
    //input
    int count;
    //internal
    short *elev_ai16;
    unsigned char *cindex_au8;
    unsigned char *cold_au8;
    unsigned char *temp_au8;
    float *lat1;
    float *lon1;
    float *lat2;
    float *lon2;
    WorkFbmOnSphereN wrkNoise1;
    float *noise1;
    WorkFbmOnSphereN wrkNoise2;
    float *noise2;
} WorkBiomColor;

void WorkBiomColor_init(WorkBiomColor* wrk, int count) {
    wrk->count = count;
    wrk->elev_ai16 = (short*)malloc(sizeof(short) * count);
    wrk->cindex_au8 = (unsigned char*)malloc(sizeof(unsigned char) * count);
    wrk->cold_au8 = (unsigned char*)malloc(sizeof(unsigned char) * count);
    wrk->temp_au8 = (unsigned char*)malloc(sizeof(unsigned char) * count);
    wrk->lat1 = (float*)malloc(sizeof(float) * count);
    wrk->lon1 = (float*)malloc(sizeof(float) * count);
    wrk->lat2 = (float*)malloc(sizeof(float) * count);
    wrk->lon2 = (float*)malloc(sizeof(float) * count);
    wrk->noise1 = (float*)malloc(sizeof(float) * count);
    wrk->noise2 = (float*)malloc(sizeof(float) * count);
    WorkFbmOnSphereN_init(&wrk->wrkNoise1, count);
    WorkFbmOnSphereN_init(&wrk->wrkNoise2, count);
}
void WorkBiomColor_free(WorkBiomColor* wrk) {
    free(wrk->elev_ai16);
    free(wrk->cindex_au8);
    free(wrk->cold_au8);
    free(wrk->temp_au8);
    free(wrk->lat1);
    free(wrk->lon1);
    free(wrk->lat2);
    free(wrk->lon2);
    WorkFbmOnSphereN_free(&wrk->wrkNoise1);
    WorkFbmOnSphereN_free(&wrk->wrkNoise2);
    free(wrk->noise1);
    free(wrk->noise2);
    wrk->count = 0;
}
void WorkBiomColor_compute(
    WorkBiomColor *wrk,
    LatData *pLatData, float *lon_a, float *buf_noise, MapPoint *pdata, int count)
{
    // Thease are the same for one latitude
    // and can be calculated once.
    const float lat = pLatData->lat;
    const float base = pLatData->base;
    const float jet = pLatData->jet;
    const double baseXjet = base * jet;
    const float lat_ratio = pLatData->lat_ratio;
    const unsigned char temp_lat_u8 = 255 * (1.0 - lat_ratio);

    // get and saturate noise to elevation
    unit_short_saturate_n(buf_noise, wrk->elev_ai16, count);
    // find color index
    find_color_index_n(pLatData->edges, wrk->elev_ai16, wrk->cindex_au8, count);
    // set map cells to basic values
    for(int i = 0; i < count; i++) {
        pdata[i].flags = 0;
        pdata[i].temp = temp_lat_u8; // actually it is the same. TODO: change temp depending on elevation?
        pdata[i].elevation = wrk->elev_ai16[i];
    }
    // weather calculation
    for (int i = 0; i < count; i++) {
        wrk->lat1[i] = lat + 40.0f;
        wrk->lon1[i] = lon_a[i] - 40.0f;
    }
    WorkFbmOnSphereN_compute(
        &wrk->wrkNoise1,
        wrk->lat1, wrk->lon1, 3.0f,
        wrk->noise1,
        count,
        perlin3n
    );
    for (int i = 0; i < count; i++) {
        wrk->lat2[i] = lat + wrk->noise1[i] * 20.0f;
        wrk->lon2[i] = lon_a[i] + wrk->noise1[i] * 20.0f;
    }
    WorkFbmOnSphereN_compute(
        &wrk->wrkNoise2,
        wrk->lat2, wrk->lon2, 8.0f,
        wrk->noise2,
        count,
        perlin3n
    );
    for (int i = 0; i < count; i++) {
        float fbm1 = wrk->noise1[i] * 0.5f + 0.5f;
        float fbm2 = wrk->noise2[i] * 0.5f + 0.5f;
        float temp_factor = fminf(fmaxf(temp_lat_u8 / 256.0f, 0.0f), 1.0f);
        double cloud_raw = baseXjet * fbm1 * fbm2 * temp_factor* 2.0f;
        cloud_raw = fmaxf(0.0f, fminf(cloud_raw, 1.0f));
        unsigned char alpha = (cloud_raw > 0.05f) ? (unsigned char)(powf(cloud_raw, 1.2f) * 255.0f) : 0;
        pdata[i].precip = alpha;
        wrk->cold_au8[i] = 0;
    }
    // poles
    if (pLatData->abslat > 70.0f) {
        perlin_variation_n(pLatData->lat, lon_a, wrk->noise1, count);
        for (int i = 0; i < count; i++) {
            if (wrk->noise1[i] < (pLatData->abslat - 77.0f)) {
                wrk->cold_au8[i] = 1U;
            }   
        }
    }
    // set color
    for(int i = 0; i < count; i++, pdata++) {
        unsigned char cindex = wrk->cindex_au8[i];
        if (wrk->cold_au8[i]) {
            pdata->r = g_elevclass_cold_param[cindex].r;
            pdata->g = g_elevclass_cold_param[cindex].g;
            pdata->b = g_elevclass_cold_param[cindex].b;
            pdata->flags |= FLAG_COLD;
        } else {
            pdata->r = g_elevclass_param[cindex].r;
            pdata->g = g_elevclass_param[cindex].g;
            pdata->b = g_elevclass_param[cindex].b;
        }
        if (cindex <= MAPGEN_ELEVCLASS_COASTLINE){
            pdata->flags |= FLAG_UNDERWATER;
            pdata->elevation = 0; // this can be done later during rendering, but it is here now.
        }
    }
}

void mapgen_generate(void) {
    int i, j;
    float buf_lat[BLOCK_SIZE], buf_lon[BLOCK_SIZE], buf_noise[BLOCK_SIZE];
    WorkFbmOnSphereN wrk;
    WorkFbmOnSphereN_init(&wrk, BLOCK_SIZE);
    WorkBiomColor wrk_biomcolor;
    WorkBiomColor_init(&wrk_biomcolor, BLOCK_SIZE);

    MapPoint* pdata=g_map.mapdata;
    for (i = 0; i < LAT_POINTS; i++) {
        double lat = -90.0f + (double)i * MAPGEN_RESOLUTION;
        LatData latdata;
        init_lat_data(&latdata, lat);
        for (j = 0; j < LON_POINTS; j += BLOCK_SIZE) {
            int count = (j + BLOCK_SIZE <= LON_POINTS) ? BLOCK_SIZE : (LON_POINTS - j);
    
            for (int k = 0; k < count; k++) {
                double lon = -180.0f + (double)(j + k) * MAPGEN_RESOLUTION;
                buf_lat[k] = lat;
                buf_lon[k] = lon;
            }
    
            // SIMD verzió (ha majd lesz)
            WorkFbmOnSphereN_compute(
                &wrk,
                buf_lat, buf_lon, 3.0f,
                buf_noise,
                count,
                perlin3n
            );
            WorkBiomColor_compute(
                &wrk_biomcolor,
                &latdata, buf_lon, buf_noise, pdata, count
            );
            pdata += count;
        }
    }
    g_map.need_update = 1;
    WorkFbmOnSphereN_free(&wrk);
    WorkBiomColor_free(&wrk_biomcolor);
}
