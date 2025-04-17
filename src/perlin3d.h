#ifndef PERLIN3D_H
#define PERLIN3D_H

#define BLOCK_SIZE (180)

typedef struct {
    float x, y, z, a;
} Point3D;

void init_perlin();
float perlin3(float x, float y, float z);

void perlin3n(float* x, float* y, float* z, float* out, int n);

float perlin_sphere(float lat_deg, float lon_deg, float radius);
float perlin_variation(float lat_deg, float lon_deg);
float perlin_variation_n(float lat_deg, float *lon_deg, float *out, int n);
#endif
