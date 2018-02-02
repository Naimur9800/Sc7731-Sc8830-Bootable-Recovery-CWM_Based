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

#ifndef _MINUI_H_
#define _MINUI_H_

#include <stdbool.h>

#ifndef SYN_REPORT
#define SYN_REPORT          0x00
#endif
#ifndef SYN_CONFIG
#define SYN_CONFIG          0x01
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT       0x02
#endif

#define ABS_MT_POSITION     0x2a
#define ABS_MT_AMPLITUDE    0x2b
#define ABS_MT_SLOT         0x2f
#define ABS_MT_TOUCH_MAJOR  0x30
#define ABS_MT_TOUCH_MINOR  0x31
#define ABS_MT_WIDTH_MAJOR  0x32
#define ABS_MT_WIDTH_MINOR  0x33
#define ABS_MT_ORIENTATION  0x34
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TOOL_TYPE    0x37
#define ABS_MT_BLOB_ID      0x38
#define ABS_MT_TRACKING_ID  0x39
#define ABS_MT_PRESSURE     0x3a
#define ABS_MT_DISTANCE     0x3b

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    int row_bytes;
    int pixel_bytes;
    unsigned char* data;
    __u32 format;
} GRSurface;

typedef void* gr_surface;
typedef unsigned short gr_pixel;

int gr_init(void);
void gr_exit(void);

int gr_fb_width(void);
int gr_fb_height(void);
gr_pixel *gr_fb_data(void);
void gr_flip(void);
void gr_fb_blank(bool blank);

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_fill(int x1, int y1, int x2, int y2);
int gr_text(int x, int y, const char *s, int bold);
void gr_texticon(int x, int y, gr_surface icon);
void gr_font_size(int *x, int *y);
void gr_get_memory_surface(gr_surface);

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy);
unsigned int gr_get_width(gr_surface surface);
unsigned int gr_get_height(gr_surface surface);

// input event structure, include <linux/input.h> for the definition.
// see http://www.mjmwired.net/kernel/Documentation/input/ for info.
struct input_event;

typedef int (*ev_callback)(int fd, uint32_t epevents, void *data);
typedef int (*ev_set_key_callback)(int code, int value, void *data);

int ev_init(ev_callback input_cb, void *data);
void ev_exit(void);
int ev_add_fd(int fd, ev_callback cb, void *data);
int ev_sync_key_state(ev_set_key_callback set_key_cb, void *data);

/* timeout has the same semantics as for poll
 *    0 : don't block
 *  < 0 : block forever
 *  > 0 : block for 'timeout' milliseconds
 */
int ev_wait(int timeout);

int ev_get_input(int fd, uint32_t epevents, struct input_event *ev);
void ev_dispatch(void);
int ev_get_epollfd(void);

// Resources

int res_create_display_surface(const char* name, gr_surface* pSurface);
int res_create_multi_display_surface(const char* name,
                                     int* frames, gr_surface** pSurface);

void res_free_surface(gr_surface surface);
// These are new graphics functions from 5.0 that were not available in
// 4.4 that are required by charger and healthd
void gr_clear();

int vibrate(int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
