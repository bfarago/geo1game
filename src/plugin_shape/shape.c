#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <unistd.h>

#include "shape.h"
#include "plan.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

void free_shape(Shape *s) {
    free(s->px);
    free(s->py);
    free(s->pz);
    free(s->fa);
    free(s->fb);
    free(s->fc);
    free(s->r);
    free(s->g);
    free(s->b);
    free(s->color_indices);
}

void init_shape(Shape *s, int vcount, int fcount, int ccount) {
    s->vertex_count = vcount;
    s->face_count = fcount;
    s->color_count = ccount;
    s->px = malloc(vcount * sizeof(float));
    s->py = malloc(vcount * sizeof(float));
    s->pz = malloc(vcount * sizeof(float));
    s->fa = malloc(fcount * sizeof(int));
    s->fb = malloc(fcount * sizeof(int));
    s->fc = malloc(fcount * sizeof(int));
    s->r = malloc(ccount * sizeof(unsigned char));
    s->g = malloc(ccount * sizeof(unsigned char));
    s->b = malloc(ccount * sizeof(unsigned char));
    s->color_indices = malloc(vcount * sizeof(int));
}

// Create a box centered at (cx, cy, cz) with width w, height h, depth d
Shape create_box(float cx, float cy, float cz, float w, float h, float d, int color_index) {
    Shape s;
    init_shape(&s, 8, 12, 1);

    float x = w / 2.0f, y = h / 2.0f, z = d / 2.0f;
    float verts[8][3] = {
        {cx - x, cy - y, cz - z}, {cx + x, cy - y, cz - z},
        {cx + x, cy + y, cz - z}, {cx - x, cy + y, cz - z},
        {cx - x, cy - y, cz + z}, {cx + x, cy - y, cz + z},
        {cx + x, cy + y, cz + z}, {cx - x, cy + y, cz + z},
    };
    for (int i = 0; i < 8; ++i) {
        s.px[i] = verts[i][0];
        s.py[i] = verts[i][1];
        s.pz[i] = verts[i][2];
        s.color_indices[i] = color_index;
    }
    int tris[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {1, 5, 6}, {1, 6, 2},
        {5, 4, 7}, {5, 7, 6}, {4, 0, 3}, {4, 3, 7},
        {3, 2, 6}, {3, 6, 7}, {4, 5, 1}, {4, 1, 0}
    };
    for (int i = 0; i < 12; ++i) {
        s.fa[i] = tris[i][0];
        s.fb[i] = tris[i][1];
        s.fc[i] = tris[i][2];
    }
    s.r[0] = 180; s.g[0] = 180; s.b[0] = 255;
    return s;
}


int append_cylinder(ShipModelParams *smparams, float cx, float cy, float cz, float radius, float height, int rings, int segments, int color_index) {
    int base_vertex = smparams->vertex_count;
    int base_face = smparams->face_count;

    int new_vertices = rings * segments;
    int new_faces = 2 * (rings - 1) * (segments - 1);

    smparams->vertex_count += new_vertices;
    smparams->face_count += new_faces;

    smparams->px = realloc(smparams->px, smparams->vertex_count * sizeof(float));
    smparams->py = realloc(smparams->py, smparams->vertex_count * sizeof(float));
    smparams->pz = realloc(smparams->pz, smparams->vertex_count * sizeof(float));
    smparams->color_indices = realloc(smparams->color_indices, smparams->vertex_count * sizeof(int));

    smparams->fa = realloc(smparams->fa, smparams->face_count * sizeof(int));
    smparams->fb = realloc(smparams->fb, smparams->face_count * sizeof(int));
    smparams->fc = realloc(smparams->fc, smparams->face_count * sizeof(int));

    int vi = base_vertex;
    for (int i = 0; i < rings; ++i) {
        float z = cz + (float)i / (rings - 1) * height;
        for (int j = 0; j < segments; ++j) {
            float theta = 2.0f * M_PI * j / segments;
            smparams->px[vi] = cx + radius * cosf(theta);
            smparams->py[vi] = cy + radius * sinf(theta);
            smparams->pz[vi] = z;
            smparams->color_indices[vi] = color_index;
            vi++;
        }
    }

    int fi = base_face;
    for (int i = 0; i < rings - 1; ++i) {
        for (int j = 0; j < segments - 1; ++j) {
            int a = base_vertex + i * segments + j;
            int b = a + 1;
            int c = a + segments;
            int d = c + 1;
            smparams->fa[fi] = a;
            smparams->fb[fi] = b;
            smparams->fc[fi] = c;
            fi++;
            smparams->fa[fi] = b;
            smparams->fb[fi] = d;
            smparams->fc[fi] = c;
            fi++;
        }
    }

    return 0;
}

void rotate_shape(Shape *shape, float rx, float ry, float rz) {
    float sinx = sinf(rx), cosx = cosf(rx);
    float siny = sinf(ry), cosy = cosf(ry);
    float sinz = sinf(rz), cosz = cosf(rz);

    for (int i = 0; i < shape->vertex_count; i++) {
        float x = shape->px[i];
        float y = shape->py[i];
        float z = shape->pz[i];

        // Z tengely körüli forgatás
        float x1 = x * cosz - y * sinz;
        float y1 = x * sinz + y * cosz;
        float z1 = z;

        // Y tengely körüli forgatás
        float x2 = x1 * cosy + z1 * siny;
        float y2 = y1;
        float z2 = -x1 * siny + z1 * cosy;

        // X tengely körüli forgatás
        float x3 = x2;
        float y3 = y2 * cosx - z2 * sinx;
        float z3 = y2 * sinx + z2 * cosx;

        shape->px[i] = x3;
        shape->py[i] = y3;
        shape->pz[i] = z3;
    }
}
void append_shape_to_model(ShipModelParams *model, Shape *shape) {
    int old_vertex_count = model->vertex_count;
    int old_face_count = model->face_count;

    // Új elemek számítása
    int new_vertex_count = old_vertex_count + shape->vertex_count;
    int new_face_count = old_face_count + shape->face_count;

    // Realloc minden vertex és face bufferre
    model->px = realloc(model->px, new_vertex_count * sizeof(float));
    model->py = realloc(model->py, new_vertex_count * sizeof(float));
    model->pz = realloc(model->pz, new_vertex_count * sizeof(float));
    model->color_indices = realloc(model->color_indices, new_vertex_count * sizeof(int));

    model->fa = realloc(model->fa, new_face_count * sizeof(int));
    model->fb = realloc(model->fb, new_face_count * sizeof(int));
    model->fc = realloc(model->fc, new_face_count * sizeof(int));

    // Másolás vertexek
    for (int i = 0; i < shape->vertex_count; i++) {
        model->px[old_vertex_count + i] = shape->px[i];
        model->py[old_vertex_count + i] = shape->py[i];
        model->pz[old_vertex_count + i] = shape->pz[i];
        model->color_indices[old_vertex_count + i] = shape->color_indices ? shape->color_indices[i] : 0;
    }

    // Másolás face-ek, offsetelve az új vertex indexekhez
    for (int i = 0; i < shape->face_count; i++) {
        model->fa[old_face_count + i] = shape->fa[i] + old_vertex_count;
        model->fb[old_face_count + i] = shape->fb[i] + old_vertex_count;
        model->fc[old_face_count + i] = shape->fc[i] + old_vertex_count;
    }

    // Frissítjük a számlálókat
    model->vertex_count = new_vertex_count;
    model->face_count = new_face_count;
}

void translate_shape(Shape *shape, float dx, float dy, float dz)
 {
    for (int i = 0; i < shape->vertex_count; i++) {
        shape->px[i] += dx;
        shape->py[i] += dy;
        shape->pz[i] += dz;
    }
}

void synthesize_shipmodel(ShipModelParams *smparams, ObjectPlan *plan) {
    init_shipmodel(smparams, 0, 0, 3);
    smparams->r[0] = 180; smparams->g[0] = 180; smparams->b[0] = 255; // hull
    smparams->r[1] = 255; smparams->g[1] = 80;  smparams->b[1] = 80;  // chimney
    smparams->r[2] = 255; smparams->g[2] = 255; smparams->b[2] = 255; // cabin
    float len=plan->plan.ship.Len;
    float width=plan->plan.ship.Width;
    float height=plan->plan.ship.Height;
    float chimney_angle=90*M_PI/180.0f; // default chimney angle
    float chimney_height=0.5f;
    
    if (plan->type == ShipFishing) {
        chimney_angle += 6.0 * M_PI / 180.0f ; //lan->plan.ship.ChimneyAngle;
    }
    // 1. main hull
    Shape hull = create_box(0, 0, 0, width, 0.5f, len, 0);
    //translate_shape(&hull, 0, 1.3f, 2.0f);

    // 2. stern elevation (rear platform)
    Shape stern = create_box(0, 0, 0, 0.90f*width, 0.5f*height, 0.1f*len, 0);
    translate_shape(&stern, 0, 0.025f, 0.5f*len);

    // 3. cabin (bridge)
    Shape cabin = create_box(0, 0.0, 0, 0.66f*width, 0.5f, 0.3f*len, 2);
    translate_shape(&cabin, 0, 0.1f, 0.25f*len);

    // 4. chimney
    Shape chimney = create_box(0,0,0, 0.1, 0.1, chimney_height, 1);
    rotate_shape(&chimney, chimney_angle, 0, M_PI/2.0f); // (z, y, x)
    translate_shape(&chimney, 0, 0.5f, 0.1f*len);
    
    // 5. front taper (fake slant with boxes)
    Shape tip1 = create_box(0, 0.0f, 0, 0.5f, 0.5f, 0.5f, 0);
    rotate_shape(&tip1, 0, 0, -M_PI / 4.0f);
    translate_shape(&tip1,0, 0.0f, -0.25f*len);

    Shape tip2 = create_box(0, 0.0f, 0, 0.5f, 0.5f, 0.5f, 0);
    rotate_shape(&tip2, 0, 0, -M_PI / 4.0f);
    translate_shape(&tip2, 0, 0.0f, -0.5f*len);

    // add parts
    append_shape_to_model(smparams, &hull);
    append_shape_to_model(smparams, &stern);
    append_shape_to_model(smparams, &cabin);
    append_shape_to_model(smparams, &chimney);
    append_shape_to_model(smparams, &tip1);
    append_shape_to_model(smparams, &tip2);

    // cleanup
    free_shape(&hull);
    free_shape(&stern);
    free_shape(&cabin);
    free_shape(&chimney);
    free_shape(&tip1);
    free_shape(&tip2);
}

void synthesize_shipmodel6(ShipModelParams *smparams) {
    init_shipmodel(smparams, 0, 0, 2); 
    smparams->r[0] = 180; smparams->g[0] = 180; smparams->b[0] = 255; // hajó
    smparams->r[1] = 255; smparams->g[1] = 80;  smparams->b[1] = 80;  // kémény

    Shape box1 = create_box(0, 0, -5.0f, 1.0f, 1.0f, 10.0f, 0);
    Shape box2 = create_box(0, 0.5f, 0.0f, 0.1f, 2.0f, 5.0f, 1);
    translate_shape(&box2, 0.2f, 0.0f, 0.0f);
    rotate_shape(&box2, 0.0f, 0.0f, M_PI / 6.0f); // forgatás Z tengely körül
    append_shape_to_model(smparams, &box1);
    append_shape_to_model(smparams, &box2);

    /*
    Shape sum = create_sum(&box1, &rotated_box2);
    Shape sub = create_sub(&box1, &rotated_box2);
    Shape uni = create_union(&box1, &rotated_box2);
    append_shape_to_model(smparams, &sum);
    append_shape_to_model(smparams, &sub);
    append_shape_to_model(smparams, &uni);
    */
    free_shape(&box1);
    free_shape(&box2);
    /*free_shape(&sum);
    free_shape(&sub);
    free_shape(&uni);*/
}

void synthesize_shipmodel5(ShipModelParams *smparams) {
    init_shipmodel(smparams, 0, 0, 2); // Üresen indulunk, színek lesznek csak

    // Színek
    smparams->r[0] = 180; smparams->g[0] = 180; smparams->b[0] = 255; // hajó
    smparams->r[1] = 255; smparams->g[1] = 80;  smparams->b[1] = 80;  // kémény

    append_cylinder(smparams, 0, 0, -5.0f, 0.9f, 10.0f, 20, 20, 0); // hajótest
    append_cylinder(smparams, 0, 0.5f, 0.0f, 0.1f, 2.0f, 5, 16, 1);  // kémény
}
void synthesize_shipmodel4(ShipModelParams *smparams) {
    int rings = 10;
    int segments = 20;
    float length = 10.0f;
    float radius = 1.0f;

    int top_offset = 0;
    int bottom_offset = rings * segments;
    (void)top_offset; // suppress unused variable warning
    (void)bottom_offset; // suppress unused variable warning

    int vertex_count = 2 * rings * segments;
    int face_count = 4 * (rings - 1) * (segments - 1);

    init_shipmodel(smparams, vertex_count, face_count, 2); // minden vertex saját színt kap most
    smparams->id = 1;

    smparams->r[0] = 180; smparams->g[0] = 180; smparams->b[0] = 255; // ship hull
    smparams->r[1] = 255; smparams->g[1] = 80;  smparams->b[1] = 80;  // chimney

    int vi = 0;
    for (int i = 0; i < rings; ++i) {
        float z = -length / 2 + length * i / (rings - 1);
        float taper = 1.0f - fabsf((float)i / (rings - 1) - 0.5f) * 2.0f;
        for (int j = 0; j < segments; ++j) {
            float theta = (float)j / segments * 2 * M_PI;
            float x = radius * taper * cosf(theta);
            float y = radius * taper * sinf(theta);
            smparams->px[vi] = x;
            smparams->py[vi] = y;
            smparams->pz[vi] = z;
            smparams->color_indices[vi] = 0;
            vi++;
        }
    }
    // Bottom half (mirror)
    for (int i = 0; i < rings * segments; ++i) {
        smparams->px[vi] = smparams->px[i];
        smparams->py[vi] = -smparams->py[i];
        smparams->pz[vi] = smparams->pz[i];
        smparams->color_indices[vi] = 0;
        vi++;
    }

    // Triangles
    int fi = 0;
    for (int i = 0; i < rings - 1; ++i) {
        for (int j = 0; j < segments - 1; ++j) {
            int a = i * segments + j;
            int b = a + 1;
            int c = a + segments;
            int d = c + 1;
            smparams->fa[fi] = a;
            smparams->fb[fi] = b;
            smparams->fc[fi] = c;
            fi++;
            smparams->fa[fi] = b;
            smparams->fb[fi] = d;
            smparams->fc[fi] = c;
            fi++;
        }
    }

    // Chimney (cylinder)
    float cx = 0, cy = radius * 0.5f, cz = 0;
    float cr = 0.1f;
    (void)cz; // suppress unused variable warning
    int chimney_segments = 16;
    int chimney_rings = 5;
    int chimney_start = smparams->vertex_count;
    int chimney_faces = 2 * (chimney_rings - 1) * (chimney_segments - 1);
    smparams->vertex_count += chimney_segments * chimney_rings;
    smparams->face_count += chimney_faces;
    smparams->px = realloc(smparams->px, smparams->vertex_count * sizeof(float));
    smparams->py = realloc(smparams->py, smparams->vertex_count * sizeof(float));
    smparams->pz = realloc(smparams->pz, smparams->vertex_count * sizeof(float));
    smparams->color_indices = realloc(smparams->color_indices, smparams->vertex_count * sizeof(int));
    smparams->fa = realloc(smparams->fa, smparams->face_count * sizeof(int));
    smparams->fb = realloc(smparams->fb, smparams->face_count * sizeof(int));
    smparams->fc = realloc(smparams->fc, smparams->face_count * sizeof(int));

    for (int i = 0; i < chimney_rings; ++i) {
        float z = 0.0f + i * 0.4f;
        for (int j = 0; j < chimney_segments; ++j) {
            float theta = 2.0f * M_PI * j / chimney_segments;
            float x = cx + cr * cosf(theta);
            float y = cy + cr * sinf(theta);
            int idx = chimney_start + i * chimney_segments + j;
            smparams->px[idx] = x;
            smparams->py[idx] = y;
            smparams->pz[idx] = z;
            smparams->color_indices[idx] = 1;
        }
    }
    int base_face = fi;
    (void)base_face; // suppress unused variable warning, what was the plan with this variable?
    for (int i = 0; i < chimney_rings - 1; ++i) {
        for (int j = 0; j < chimney_segments - 1; ++j) {
            int a = chimney_start + i * chimney_segments + j;
            int b = a + 1;
            int c = a + chimney_segments;
            int d = c + 1;
            smparams->fa[fi] = a;
            smparams->fb[fi] = b;
            smparams->fc[fi] = c;
            fi++;
            smparams->fa[fi] = b;
            smparams->fb[fi] = d;
            smparams->fc[fi] = c;
            fi++;
        }
    }
}

void synthesize_shipmodel3(ShipModelParams *smparams) {
    int segments = 12;      // keresztmetszet pontok száma (köríven)
    int slices = 30;        // hosszanti szeletek (hossz mentén)
    float length = 10.0f;   // hajó hossza
    float base_width = 2.0f;
    float base_height = 1.0f;

    int vcount = segments * slices;
    int fcount = (segments - 1) * (slices - 1) * 2;

    init_shipmodel(smparams, vcount, fcount, vcount); // minden vertex saját színt kap most
    smparams->id = 1;

    int vi = 0;
    for (int i = 0; i < slices; i++) {
        float z = -length / 2 + length * (float)i / (float)(slices - 1); // orr-far irány
        float t = (float)i / (float)(slices - 1); // 0..1 közötti normalizált pozíció
        float width = base_width * (1.0f - powf(t, 1.3f));   // orr felé szűkül
        float height = base_height * (1.0f - powf(t, 1.1f)); // orr felé szűkül
        for (int j = 0; j < segments; j++) {
            float angle = M_PI * (float)j / (float)(segments - 1); // félkör (felső ív)
            float x = cosf(angle) * width;
            float y = sinf(angle) * height - 0.2f * height; // alsó él húzott
            smparams->px[vi] = x;
            smparams->py[vi] = y;
            smparams->pz[vi] = z;
            smparams->color_indices[vi] = vi;
            smparams->r[vi] = 180;
            smparams->g[vi] = 180;
            smparams->b[vi] = 255;
            vi++;
        }
    }

    int fi = 0;
    for (int i = 0; i < slices - 1; i++) {
        for (int j = 0; j < segments - 1; j++) {
            int a = i * segments + j;
            int b = a + 1;
            int c = a + segments;
            int d = c + 1;
            smparams->fa[fi] = a;
            smparams->fb[fi] = b;
            smparams->fc[fi] = c;
            fi++;
            smparams->fa[fi] = b;
            smparams->fb[fi] = d;
            smparams->fc[fi] = c;
            fi++;
        }
    }
}

void synthesize_shipmodel2(ShipModelParams *smparams) {
    int slices = 30;
    int segments = 16;
    float length = 10.0f;
    float width = 2.0f;
    float height = 1.5f;

    int vertex_count = slices * segments;
    int face_count = (slices - 1) * (segments - 1) * 2;
    int color_count = 1;

    init_shipmodel(smparams, vertex_count, face_count, color_count);

    smparams->r[0] = 180;
    smparams->g[0] = 180;
    smparams->b[0] = 255;

    for (int i = 0; i < slices; i++) {
        float z = (float)i / (slices - 1) * length - length / 2.0f;
        float taper = 1.0f - fabsf((float)i / (slices - 1) - 0.5f) * 2.0f; // orr és tat vékonyodik

        for (int j = 0; j < segments; j++) {
            float angle = (float)j / (segments - 1) * 3.14159f;
            float x = cosf(angle) * width * 0.5f * taper;
            float y = sinf(angle) * height;

            int idx = i * segments + j;
            smparams->px[idx] = x;
            smparams->py[idx] = y;
            smparams->pz[idx] = z;
            smparams->color_indices[idx] = 0;
        }
    }

    int f = 0;
    for (int i = 0; i < slices - 1; i++) {
        for (int j = 0; j < segments - 1; j++) {
            int idx0 = i * segments + j;
            int idx1 = (i + 1) * segments + j;
            int idx2 = (i + 1) * segments + (j + 1);
            int idx3 = i * segments + (j + 1);

            smparams->fa[f] = idx0;
            smparams->fb[f] = idx1;
            smparams->fc[f] = idx2;
            f++;
            smparams->fa[f] = idx0;
            smparams->fb[f] = idx2;
            smparams->fc[f] = idx3;
            f++;
        }
    }
}

void free_shipmodel(ShipModelParams *smparams) {
    free(smparams->px);
    free(smparams->py);
    free(smparams->pz);
    free(smparams->fa);
    free(smparams->fb);
    free(smparams->fc);
    free(smparams->r);
    free(smparams->g);
    free(smparams->b);
    free(smparams->color_indices);
}
void init_shipmodel(ShipModelParams *smparams, int vertex_count, int face_count, int color_count) {
    smparams->id = 0;
    smparams->vertex_count = vertex_count;
    smparams->face_count = face_count;
    smparams->color_count = color_count;
    smparams->px = malloc(smparams->vertex_count * sizeof(float));
    smparams->py = malloc(smparams->vertex_count * sizeof(float));
    smparams->pz = malloc(smparams->vertex_count * sizeof(float));
    smparams->fa = malloc(smparams->face_count * sizeof(int));
    smparams->fb = malloc(smparams->face_count * sizeof(int));
    smparams->fc = malloc(smparams->face_count * sizeof(int));
    smparams->r = malloc(smparams->color_count * sizeof(unsigned char));
    smparams->g = malloc(smparams->color_count * sizeof(unsigned char));
    smparams->b = malloc(smparams->color_count * sizeof(unsigned char));
    smparams->color_indices = malloc(smparams->vertex_count * sizeof(int));
    smparams->scale = 1.0f;
    smparams->position[0] = 0.0f;
    smparams->position[1] = 0.0f;
    smparams->position[2] = 0.0f;
    smparams->rotation[0] = 0.0f;
    smparams->rotation[1] = 0.0f;
    smparams->rotation[2] = 0.0f;
}


