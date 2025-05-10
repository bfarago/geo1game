/*
 * File:    perlin3d.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 *
 * Description:
 *   Classic Perlin noise and FBM (Fractal Brownian Motion) implementation
 *   for 3D space, optimized for procedural map and weather generation.
 *
 *   Key features:
 *     - 3D Perlin noise with fade and gradient functions
 *     - Multiple fade implementations (standard, SIMD, LUT)
 *     - SIMD-ready batched evaluation (perlin3n)
 *     - FBM-based terrain features via fbm_noise3 and fbm_on_sphere
 *
 * Usage:
 *   - Call perlin3(x, y, z) for single noise value
 *   - Call perlin3n(x[], y[], z[], out[], count) for vectorized evaluation
 *   - Call fbm_on_sphere(lat, lon, scale, func) for spherical FBM
 *
 * Dependencies:
 *   - math.h, stdlib.h, string.h
 *   - immintrin.h for SIMD intrinsics (SSE/AVX)
 *
 * Notes:
 *   - Output range of Perlin and FBM: [-1.0 .. 1.0]
 *   - Noise permutation table is extended to avoid wrapping
 *   - Performance-critical fade variants selected via FADEVERSION macro
 */
#include <math.h>
#include <stdlib.h>
#include <immintrin.h>
#include "perlin3d.h"

#define FADEVERSION (2)

#if (FADEVERSION == 2)
#define FADE_LUT_SIZE 1024
float fade_lut[FADE_LUT_SIZE];
#endif

static unsigned char permutation[256] = {
    151,160,137,91,90,15, 
    131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23, 
    190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33, 
    88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,134,139,48,27,166, 
    77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244, 
    102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196, 
    135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123, 
    5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42, 
    223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9, 
    129,22,39,253,9,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228, 
    251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107, 
    49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254, 
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static int p[512];

void init_perlin() {
    int i;
    for (i = 0; i < 256; i++) {
        p[256 + i] = p[i] = permutation[i];
    }
    #if (FADEVERSION == 2)
    for (int i = 0; i < FADE_LUT_SIZE; i++) {
        float t = (float)i / (FADE_LUT_SIZE - 1);
        fade_lut[i] = t * t * t * (t * (t * 6 - 15) + 10);
    }
    #endif
}

static inline float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

static inline float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h==12||h==14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

#if (FADEVERSION == 0)
static inline float fade(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}
static inline void fade3d(float x, float y, float z, Point3D *out){
    out->x = fade(x);
    out->y = fade(y);
    out->z = fade(z);
}
#elif (FADEVERSION == 1)
static inline void fade3d(float x, float y, float z, Point3D *out) {
    __m128 v = _mm_set_ps(0.0f, z, y, x);  // (a, z, y, x)
    
    __m128 v2 = _mm_mul_ps(v, v);            // t^2
    __m128 v3 = _mm_mul_ps(v2, v);           // t^3
    __m128 v4 = _mm_mul_ps(v2, v2);          // t^4
    __m128 v5 = _mm_mul_ps(v4, v);           // t^5

    __m128 term1 = _mm_mul_ps(v5, _mm_set1_ps(6.0f));
    __m128 term2 = _mm_mul_ps(v4, _mm_set1_ps(15.0f));
    __m128 term3 = _mm_mul_ps(v3, _mm_set1_ps(10.0f));

    __m128 fade = _mm_sub_ps(term1, term2);
    fade = _mm_add_ps(fade, term3);

    _mm_storeu_ps((float *)out, fade);  // közvetlenül a Point3D-be
}
#elif (FADEVERSION == 2)
static inline void fade3d(float x, float y, float z, Point3D *out) {
    out->x = fade_lut[(int)(x * (FADE_LUT_SIZE - 1))];
    out->y = fade_lut[(int)(y * (FADE_LUT_SIZE - 1))];
    out->z = fade_lut[(int)(z * (FADE_LUT_SIZE - 1))];
}
#endif

float perlin3(float x, float y, float z) {
    float fx = floor(x), fy = floor(y), fz = floor(z);
    int X = (int)fx & 255;
    int Y = (int)fy & 255;
    int Z = (int)fz & 255;
    x -= fx;
    y -= fy;
    z -= fz;
    Point3D f;
    fade3d(x, y, z, &f);

    int A  = p[X] + Y;
    int AA = p[A] + Z;
    int AB = p[A + 1] + Z;
    int B  = p[X + 1] + Y;
    int BA = p[B] + Z;
    int BB = p[B + 1] + Z;

    return lerp(f.z,
        lerp(f.y,
            lerp(f.x, grad(p[AA], x, y, z),
                    grad(p[BA], x - 1, y, z)),
            lerp(f.x, grad(p[AB], x, y - 1, z),
                    grad(p[BB], x - 1, y - 1, z))
        ),
        lerp(f.y,
            lerp(f.x, grad(p[AA + 1], x, y, z - 1),
                    grad(p[BA + 1], x - 1, y, z - 1)),
            lerp(f.x, grad(p[AB + 1], x, y - 1, z - 1),
                    grad(p[BB + 1], x - 1, y - 1, z - 1))
        )
    );
}

void perlin3n(float* x, float* y, float* z, float* out, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = perlin3(x[i], y[i], z[i]);
    }
}
/*************/
#ifdef AVX_IMPLEMENTATION
//#include <immintrin.h>
//#include <math.h>

//#define FADE_LUT_SIZE 256
//extern float fade_lut[FADE_LUT_SIZE];
//extern unsigned char p[512]; // permutation table

static inline __m256 fade_avx(__m256 t) {
    // fade(t) = 6t^5 - 15t^4 + 10t^3
    __m256 t2 = _mm256_mul_ps(t, t);
    __m256 t3 = _mm256_mul_ps(t2, t);
    __m256 t4 = _mm256_mul_ps(t2, t2);
    __m256 t5 = _mm256_mul_ps(t4, t);

    __m256 term1 = _mm256_mul_ps(t5, _mm256_set1_ps(6.0f));
    __m256 term2 = _mm256_mul_ps(t4, _mm256_set1_ps(15.0f));
    __m256 term3 = _mm256_mul_ps(t3, _mm256_set1_ps(10.0f));

    return _mm256_add_ps(_mm256_sub_ps(term1, term2), term3);
}

static inline __m256 lerp_avx(__m256 t, __m256 a, __m256 b) {
    return _mm256_add_ps(a, _mm256_mul_ps(t, _mm256_sub_ps(b, a)));
}

// Scalar grad for now (SIMD version would require more complex logic)
static inline __m256 grad_avx(__m256i hash, __m256 x, __m256 y, __m256 z) {
    float hx[8];
    _mm256_storeu_si256((__m256i*)hx, hash);

    float fx[8], fy[8], fz[8];
    _mm256_storeu_ps(fx, x);
    _mm256_storeu_ps(fy, y);
    _mm256_storeu_ps(fz, z);

    float result[8];
    for (int i = 0; i < 8; ++i) {
        int h = (int)hx[i] & 15;
        float u = h < 8 ? fx[i] : fy[i];
        float v = h < 4 ? fy[i] : (h == 12 || h == 14 ? fx[i] : fz[i]);
        float val = ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
        result[i] = val;
    }
    return _mm256_loadu_ps(result);
}

void perlin3n_simd(float* x, float* y, float* z, float* out, int n) {
    for (int i = 0; i < n; i += 8) {
        __m256 vx = _mm256_loadu_ps(&x[i]);
        __m256 vy = _mm256_loadu_ps(&y[i]);
        __m256 vz = _mm256_loadu_ps(&z[i]);

        __m256 fx = _mm256_floor_ps(vx);
        __m256 fy = _mm256_floor_ps(vy);
        __m256 fz = _mm256_floor_ps(vz);

        __m256i X = _mm256_and_si256(_mm256_cvttps_epi32(fx), _mm256_set1_epi32(255));
        __m256i Y = _mm256_and_si256(_mm256_cvttps_epi32(fy), _mm256_set1_epi32(255));
        __m256i Z = _mm256_and_si256(_mm256_cvttps_epi32(fz), _mm256_set1_epi32(255));

        vx = _mm256_sub_ps(vx, fx);
        vy = _mm256_sub_ps(vy, fy);
        vz = _mm256_sub_ps(vz, fz);

        __m256 fx_fade = fade_avx(vx);
        __m256 fy_fade = fade_avx(vy);
        __m256 fz_fade = fade_avx(vz);

        // A, AA, AB, B, BA, BB generation would need scalar fallback or gather emulation
        // Here: fallback to scalar per sample (to avoid complexity with permutation table access)
        for (int j = 0; j < 8; ++j) {
            int xi = ((int)floor(x[i + j])) & 255;
            int yi = ((int)floor(y[i + j])) & 255;
            int zi = ((int)floor(z[i + j])) & 255;

            float xf = x[i + j] - floor(x[i + j]);
            float yf = y[i + j] - floor(y[i + j]);
            float zf = z[i + j] - floor(z[i + j]);

            float u = fade(xf);
            float v = fade(yf);
            float w = fade(zf);

            int A  = p[xi] + yi;
            int AA = p[A] + zi;
            int AB = p[A + 1] + zi;
            int B  = p[xi + 1] + yi;
            int BA = p[B] + zi;
            int BB = p[B + 1] + zi;

            out[i + j] = lerp(w,
                lerp(v,
                    lerp(u, grad(p[AA], xf, yf, zf), grad(p[BA], xf - 1, yf, zf)),
                    lerp(u, grad(p[AB], xf, yf - 1, zf), grad(p[BB], xf - 1, yf - 1, zf))
                ),
                lerp(v,
                    lerp(u, grad(p[AA + 1], xf, yf, zf - 1), grad(p[BA + 1], xf - 1, yf, zf - 1)),
                    lerp(u, grad(p[AB + 1], xf, yf - 1, zf - 1), grad(p[BB + 1], xf - 1, yf - 1, zf - 1))
                )
            );
        }
    }
}
#endif //AVX

/*************/
float perlin_sphere(float lat_deg, float lon_deg, float radius) {
    float lat_rad = lat_deg * (3.14159265359f / 180.0f);
    float lon_rad = lon_deg * (3.14159265359f / 180.0f);

    float x = radius * cosf(lat_rad) * cosf(lon_rad);
    float y = radius * sinf(lat_rad);
    float z = radius * cosf(lat_rad) * sinf(lon_rad);

    return perlin3(x, y, z);
}

float perlin_variation(float lat_deg, float lon_deg) {
    float radius = 10.0f;
    float lat_rad = lat_deg * (3.14159265359f / 180.0f);
    float lon_rad = lon_deg * (3.14159265359f / 180.0f);

    float x = radius* cosf(lat_rad) * cosf(lon_rad);
    float y = radius * sinf(lat_rad);
    float z = radius * cosf(lat_rad) * sinf(lon_rad);

    return perlin3(x, y, z);
}

float perlin_variation_n(float lat_deg, float *lon_deg, float *out, int n) {
    float x[BLOCK_SIZE];
    float y[BLOCK_SIZE];
    float z[BLOCK_SIZE];
    for (int i = 0; i < n; i++) {
        float lon_rad = lon_deg[i] * (3.14159265359f / 180.0f);
        x[i] = 10.0f * cosf(lat_deg) * cosf(lon_rad);
        y[i] = 10.0f * sinf(lat_deg);
        z[i] = 10.0f * cosf(lat_deg) * sinf(lon_rad);
    }
    perlin3n(x, y, z, out, n);
}