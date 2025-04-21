#ifndef SHAPE_H
#define SHAPE_H

#include <stdbool.h>
#include "plan.h"
#include "global.h"
// Primitive shape type
typedef struct {
    float *px, *py, *pz; // vertex coordinates
    int *fa, *fb, *fc;   // triangle face indices
    unsigned char *r, *g, *b;
    int *color_indices;
    int vertex_count;
    int face_count;
    int color_count;
} Shape;

typedef struct {
    int id; // ship model ID
    char name[128]; // not yet used
    char description[256];  // not yet used
    float *px, *py, *pz; // vertex coordinates (nr of elements: vertex_count)
    int *fa, *fb, *fc; // face indices (triangles to vertices, nr of elements: face_count)
    unsigned char *r, *g, *b; // color values (palette, nr of elements: colour_count)
    int *color_indices; // color indices for each vertex (nr of elements: vertex_count)
    int vertex_count;
    int face_count;
    int color_count;
    // default orientation (not part of the model, but used by the renderer)
    float scale;
    float position[3];
    float rotation[3];
} ShipModelParams;

void free_shape(Shape *s);
void init_shape(Shape *s, int vcount, int fcount, int ccount);
void rotate_shape(Shape *shape, float rx, float ry, float rz);
void append_shape_to_model(ShipModelParams *model, Shape *shape);
void translate_shape(Shape *shape, float dx, float dy, float dz);

Shape create_box(float cx, float cy, float cz, float w, float h, float d, int color_index);
int append_cylinder(ShipModelParams *smparams, float cx, float cy, float cz, float radius, float height, int rings, int segments, int color_index);

void free_shipmodel(ShipModelParams *smparams);
void init_shipmodel(ShipModelParams *smparams, int vertex_count, int face_count, int color_count);
void synthesize_shipmodel(ShipModelParams *smparams, ObjectPlan *plan);
/*
// Function declarations
void initialize_shape(Shape *shape, double x, double y, double radius);
double calculate_area(const Shape *shape);
double calculate_perimeter(const Shape *shape);
bool is_point_inside(const Shape *shape, double px, double py);
*/

#endif // SHAPE_H