/*
 * File:    image.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Image abstraction layer
 * Key features:
 *  Lib PNG backend is implemented.
 */
#ifndef IMAGE_H
#define IMAGE_H
struct Image;

// Which backend is used
typedef enum {
    ImageBackend_Memory=0,
    ImageBackend_Png,
    ImageBackend_GD
} ImageBackendType;

//Colors
typedef enum {
    ImageFormat_Grayscale = 0,
    ImageFormat_RGB,
    ImageFormat_RGBA
} ImageFormat;

// Struct of Arrays or Array of structs
typedef enum {
    ImageBuffer_SoA = 0,
    ImageBuffer_AoS
} ImageBufferFormat;

//The abstract image descriptor
typedef struct Image{
    ImageBackendType backend;
    ImageFormat format;
    ImageBufferFormat buffer_format;
    unsigned int width;
    unsigned int height;
    void *backend_data;
}Image;

/*
int image_context_start();
int image_context_stop();
int image_create(Image *image, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_type);
int image_destroy(Image *image);
void image_get_buffer(Image *image, void** buffer);
*/

#endif // IMAGE_H
