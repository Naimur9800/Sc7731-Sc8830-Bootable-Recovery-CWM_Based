/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>

#include <png.h>

#include "minui.h"

#define SURFACE_DATA_ALIGNMENT 8

static GGLSurface* malloc_surface(size_t data_size) {
    size_t size = sizeof(GGLSurface) + data_size + SURFACE_DATA_ALIGNMENT;
    unsigned char* temp = malloc(size);
    if (temp == NULL) return NULL;
    GGLSurface* surface = (GGLSurface*) temp;
    surface->data = temp + sizeof(GGLSurface) +
        (SURFACE_DATA_ALIGNMENT - (sizeof(GGLSurface) % SURFACE_DATA_ALIGNMENT));
    return surface;
}

static int open_png(const char* name, png_structp* png_ptr, png_infop* info_ptr,
                    png_uint_32* width, png_uint_32* height, png_byte* channels) {
    char resPath[256];
    unsigned char header[8];
    int result = 0;
    int color_type, bit_depth;
    size_t bytesRead;

    snprintf(resPath, sizeof(resPath)-1, "/ctres/gui/%s.png", name);
    resPath[sizeof(resPath)-1] = '\0';
    FILE* fp = fopen(resPath, "rb");
    if (fp == NULL) {
        fp = fopen(name, "rb");
        if (fp == NULL) {
            result = -1;
            goto exit;
        }
    }

    bytesRead = fread(header, 1, sizeof(header), fp);
    if (bytesRead != sizeof(header)) {
        result = -2;
        goto exit;
    }

    if (png_sig_cmp(header, 0, sizeof(header))) {
        result = -3;
        goto exit;
    }

    *png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!*png_ptr) {
        result = -4;
        goto exit;
    }

    *info_ptr = png_create_info_struct(*png_ptr);
    if (!*info_ptr) {
        result = -5;
        goto exit;
    }

    if (setjmp(png_jmpbuf(*png_ptr))) {
        result = -6;
        goto exit;
    }

    png_init_io(*png_ptr, fp);
    png_set_sig_bytes(*png_ptr, sizeof(header));
    png_read_info(*png_ptr, *info_ptr);

    png_get_IHDR(*png_ptr, *info_ptr, width, height, &bit_depth,
            &color_type, NULL, NULL, NULL);

    *channels = png_get_channels(*png_ptr, *info_ptr);

    if (bit_depth == 8 && *channels == 3 && color_type == PNG_COLOR_TYPE_RGB) {
        // 8-bit RGB images: great, nothing to do.
    } else if (bit_depth <= 8 && *channels == 1 && color_type == PNG_COLOR_TYPE_GRAY) {
        // 1-, 2-, 4-, or 8-bit gray images: expand to 8-bit gray.
        png_set_expand_gray_1_2_4_to_8(*png_ptr);
    } else if (bit_depth <= 8 && *channels == 1 && color_type == PNG_COLOR_TYPE_PALETTE) {
        // paletted images: expand to 8-bit RGB.  Note that we DON'T
        // currently expand the tRNS chunk (if any) to an alpha
        // channel, because minui doesn't support alpha channels in
        // general.
        png_set_palette_to_rgb(*png_ptr);
        *channels = 3;
    } else if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(*png_ptr);
    }

    return result;

  exit:
    if (result < 0) {
        png_destroy_read_struct(png_ptr, info_ptr, NULL);
    }
    if (fp != NULL) {
        fclose(fp);
    }

    return result;
}

// "display" surfaces are transformed into the framebuffer's required
// pixel format (currently only RGBX is supported) at load time, so
// gr_blit() can be nothing more than a memcpy() for each row.  The
// next two functions are the only ones that know anything about the
// framebuffer pixel format; they need to be modified if the
// framebuffer format changes (but nothing else should).

// Allocate and return a gr_surface sufficient for storing an image of
// the indicated size in the framebuffer pixel format.
static GGLSurface* init_display_surface(png_uint_32 width, png_uint_32 height) {
    GGLSurface* surface;

    surface = (GGLSurface*) malloc_surface(width * height * 4);
    if (surface == NULL) return NULL;

    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width;

    return surface;
}

// Copy 'input_row' to 'output_row', transforming it to the
// framebuffer pixel format.  The input format depends on the value of
// 'channels':
//
//   1 - input is 8-bit grayscale
//   3 - input is 24-bit RGB
//   4 - input is 32-bit RGBA/RGBX
//
// 'width' is the number of pixels in the row.
static void transform_rgb_to_draw(unsigned char* input_row,
                                  unsigned char* output_row,
                                  int channels, int width) {
    int x;
    unsigned char* ip = input_row;
    unsigned char* op = output_row;

    switch (channels) {
        case 1:
            // expand gray level to RGBX
            for (x = 0; x < width; ++x) {
                *op++ = *ip;
                *op++ = *ip;
                *op++ = *ip;
                *op++ = 0xff;
                ip++;
            }
            break;

        case 3:
            // expand RGBA to RGBX
            for (x = 0; x < width; ++x) {
                *op++ = *ip++;
                *op++ = *ip++;
                *op++ = *ip++;
                *op++ = 0xff;
            }
            break;

        case 4:
            // copy RGBA to RGBX
            memcpy(output_row, input_row, width*4);
            break;
    }
}

int res_create_display_surface(const char* name, gr_surface* pSurface) {
    GGLSurface* surface = NULL;
    int result = 0;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_uint_32 width, height;
    png_byte channels;

    *pSurface = NULL;

    result = open_png(name, &png_ptr, &info_ptr, &width, &height, &channels);
    if (result < 0) return result;

    surface = init_display_surface(width, height);
    if (surface == NULL) {
        result = -8;
        goto exit;
    }

#if defined(RECOVERY_RGBX) || defined(RECOVERY_RGBX)
    png_set_bgr(png_ptr);
#endif

    unsigned char* p_row = malloc(width * 4);
    unsigned int y;
    for (y = 0; y < height; ++y) {
        png_read_row(png_ptr, p_row, NULL);
        transform_rgb_to_draw(p_row, surface->data + y * width * 4, channels, width);
    }
    free(p_row);

    if (channels == 3)
        surface->format = GGL_PIXEL_FORMAT_RGBX_8888;
    else
        surface->format = GGL_PIXEL_FORMAT_RGBA_8888;

    *pSurface = (gr_surface) surface;

  exit:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    if (result < 0 && surface != NULL) free(surface);
    return result;
}

int res_create_multi_display_surface(const char* name, int* frames, gr_surface** pSurface) {
    gr_surface* surface = NULL;
    int result = 0;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_uint_32 width, height;
    png_byte channels;
    int i;

    *pSurface = NULL;
    *frames = -1;

    result = open_png(name, &png_ptr, &info_ptr, &width, &height, &channels);
    if (result < 0) return result;

    *frames = 1;
    png_textp text;
    int num_text;
    if (png_get_text(png_ptr, info_ptr, &text, &num_text)) {
        for (i = 0; i < num_text; ++i) {
            if (text[i].key && strcmp(text[i].key, "Frames") == 0 && text[i].text) {
                *frames = atoi(text[i].text);
                break;
            }
        }
        printf("  found frames = %d\n", *frames);
    }

    if (height % *frames != 0) {
        printf("bad height (%d) for frame count (%d)\n", height, *frames);
        result = -9;
        goto exit;
    }

    surface = malloc(*frames * sizeof(GGLSurface));
    if (surface == NULL) {
        result = -8;
        goto exit;
    }
    for (i = 0; i < *frames; ++i) {
        surface[i] = init_display_surface(width, height / *frames);
        if (surface[i] == NULL) {
            result = -8;
            goto exit;
        }
    }

#if defined(RECOVERY_RGBX) || defined(RECOVERY_RGBX)
    png_set_bgr(png_ptr);
#endif

    unsigned char* p_row = malloc(width * 4);
    unsigned int y;
    for (y = 0; y < height; ++y) {
        png_read_row(png_ptr, p_row, NULL);
        int frame = y % *frames;
        GGLSurface* p = (GGLSurface*) surface[frame];
        unsigned char* out_row = p->data +
            (y / *frames) * width * 4;
        transform_rgb_to_draw(p_row, out_row, channels, width);
    }
    free(p_row);

    for (i = 0; i < *frames; ++i) {
        GGLSurface* p = (GGLSurface*) surface[i];
        if (channels == 3)
            p->format = GGL_PIXEL_FORMAT_RGBX_8888;
        else
            p->format = GGL_PIXEL_FORMAT_RGBA_8888;
    }

    *pSurface = (gr_surface*) surface;

exit:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    if (result < 0) {
        if (surface) {
            for (i = 0; i < *frames; ++i) {
                if (surface[i]) free(surface[i]);
            }
            free(surface);
        }
    }
    return result;
}

void res_free_surface(gr_surface surface) {
    GGLSurface* pSurface = (GGLSurface*) surface;
    if (pSurface) {
        free(pSurface);
    }
}
