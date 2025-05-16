/*
 * File:    perlin3d.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Perlin noise and FBM (Fractal Brownian Motion) implementation
 * for 3D space, optimized for procedural map and weather generation.
 * Key features:
 *   - 3D Perlin noise with fade and gradient functions
 *   - Multiple fade implementations (standard, SIMD, LUT)
 *   - SIMD-ready batched evaluation (perlin3n)
 *   - FBM-based terrain features via fbm_noise3 and fbm_on_sphere
 */
#ifndef PERLIN3D_H
#define PERLIN3D_H

/** Number of points in a block used in computational kernels. */
#define BLOCK_SIZE (1800)

/** Point structure.
 * This structure represents a point in 3D space with x, y, z coordinates and an additional value 'a'.
 * It is used for various calculations in the Perlin noise and FBM functions. The variable shall be
 * aligned to 16 bytes for optimal SIMD performance. Thats why we use 4 floats. Typical simd load/store
 * isntructions supports this format, no additional memory move is needed when using AVX or SSE.
 */
typedef struct {
    float x, y, z, a;
} Point3D;

/** Perlin noise functions.
 * These functions are used to generate Perlin noise in 3D space.
 * The main function is perlin3(), which computes the noise value for given x, y, z coordinates.
 * The perlin3n() function computes the noise for multiple points in a single call, using SIMD
 * instructions for performance optimization.
 * The perlin_sphere() function generates noise based on latitude and longitude coordinates on a sphere.
 * The perlin_variation() function generates noise based on latitude and longitude coordinates, but
 * with a different scaling factor.
 * The perlin_variation_n() function computes the noise for multiple latitude and longitude pairs.
 */

/** Init perlin internal variables */
void init_perlin();

/** Perlin noise function, scalar. */
float perlin3(float x, float y, float z);

/** Perlin noise function, SIMD. */
void perlin3n(float* x, float* y, float* z, float* out, int n);

/** Spherical Perlin noise function, scalar. */
float perlin_sphere(float lat_deg, float lon_deg, float radius);

/**********************************************************************************************/
/** Variations are usacase specific, when we need to vary some of the parameters of the
 * terrain or waeather, or cold zones. The main difference is the scaling factor used in the
 * specialized usecase. This fns can be moved to a higher layer, but for now it is here.
 */

/** Spherical Perlin noise function for variations
 *  In case of parameter variation is needed
 */
float perlin_variation(float lat_deg, float lon_deg);

/** Spherical Perlin noise function for variations
 *  In case of parameter variation is needed
 */
void perlin_variation_n(float lat_deg, float *lon_deg, float *out, int n);

/* FBM (Fractal Brownian Motion) functions could be placed here, or a higher layer. */

#endif
