/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "minui.h"
#include "graphics.h"

static gr_surface fbdev_init(minui_backend*);
static gr_surface fbdev_flip(minui_backend*);
static void fbdev_blank(minui_backend*, bool);
static void fbdev_exit(minui_backend*);

static GRSurface gr_framebuffer[2];
static bool double_buffered;
static GRSurface* gr_draw = NULL;
static int displayed_buffer;

static struct fb_var_screeninfo vi;
static int fb_fd = -1;

static minui_backend my_backend = {
    .init = fbdev_init,
    .flip = fbdev_flip,
    .blank = fbdev_blank,
    .exit = fbdev_exit,
};

minui_backend* open_fbdev() {
    return &my_backend;
}

static void fbdev_blank(minui_backend* backend __unused, bool blank)
{
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}

static void set_displayed_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffered) return;

    vi.yres_virtual = gr_framebuffer[0].height * 2;
    vi.yoffset = n * gr_framebuffer[0].height;
    vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
    displayed_buffer = n;
}

/* MTK rorate --- */
static GRSurface* gr_canvas = NULL;
static int rotate_index=-1;

static void print_surface_info(GRSurface *s, const char *name)
{
    printf("[graphics] %s > Height:%d, Width:%d, PixelBytes:%d, RowBytes:%d, Size:%d\n",
        name, s->height, s->width, s->pixel_bytes, s->row_bytes, s->height* s->row_bytes);
}

// Read configuration from MTK_LCM_PHYSICAL_ROTATION
static int rotate_config(GRSurface *gr_draw)
{
    if (rotate_index<0)
    {
        if (gr_draw->pixel_bytes != 4) rotate_index=0; // support 4 bytes pixel only
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) rotate_index=1;
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "180", 3)) rotate_index=2;
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3)) rotate_index=3;
        else rotate_index=0;
        printf("[graphics] rotate_config %d %s\n", rotate_index, MTK_LCM_PHYSICAL_ROTATION);
    }
    return rotate_index;
}

#define swap(x, y, type) {type z; z=x; x=y; y=z;}

// Allocate and setup the canvas object
static void rotate_canvas_init(GRSurface *gr_draw)
{
    gr_canvas = (GRSurface*) malloc(sizeof(GRSurface));
    memcpy(gr_canvas, gr_draw, sizeof(GRSurface));

    // Swap canvas' height and width, if the rotate angle is 90" or 270"
    if (rotate_config(gr_draw)%2) {
        swap(gr_canvas->width, gr_canvas->height, int);
        gr_canvas->row_bytes = gr_canvas->width * gr_canvas->pixel_bytes;
    }

    gr_canvas->data = (unsigned char*) malloc(gr_canvas->height * gr_canvas->row_bytes);
    memset(gr_canvas->data,  0, gr_canvas->height * gr_canvas->row_bytes);

    print_surface_info(gr_draw, "gr_draw");
    print_surface_info(gr_canvas, "gr_canvas");
}

// Cleanup the canvas
void rotate_canvas_exit(void)
{
    if (gr_canvas) {
        if (gr_canvas->data)
            free(gr_canvas->data);
        free(gr_canvas);
    }
    gr_canvas=NULL;
}

// Return the canvas object
GRSurface *rotate_canvas_get(GRSurface *gr_draw)
{
    // Initialize the canvas, if it was not exist.
    if (gr_canvas==NULL)
        rotate_canvas_init(gr_draw);
    return gr_canvas;
}

// Surface Rotate Routines
static void rotate_surface_0(GRSurface *dst, GRSurface *src)
{
    memcpy(dst->data, src->data, src->height*src->row_bytes);
}

static void rotate_surface_270(GRSurface *dst, GRSurface *src)
{
    int v, w, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0, v=src->width-1; h<dst->height; h++, v--) {
        for (w=0; w<dst->width; w++) {
            dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
            src_pixel = (unsigned int *)(src->data + src->row_bytes*w);
            *(dst_pixel+w)=*(src_pixel+v);
        }
    }
}

static void rotate_surface_180(GRSurface *dst, GRSurface *src)
{
    int v, w, k, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0, k=src->height-1; h<dst->height && k>=0 ; h++, k--) {
        dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
        src_pixel = (unsigned int *)(src->data + src->row_bytes*k);
        for (w=0, v=src->width-1; w<dst->width && v>=0; w++, v--) {
            *(dst_pixel+w)=*(src_pixel+v);
        }
    }
}

static void rotate_surface_90(GRSurface *dst, GRSurface *src)
{
    int w, k, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0; h<dst->height; h++) {
        for (w=0, k=src->height-1; w<dst->width; w++, k--) {
            dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
            src_pixel = (unsigned int *)(src->data + src->row_bytes*k);
            *(dst_pixel+w)=*(src_pixel+h);
        }
    }
}

typedef void (*rotate_surface_t) (GRSurface *, GRSurface *);

rotate_surface_t rotate_func[4]=
{
    rotate_surface_0,
    rotate_surface_90,
    rotate_surface_180,
    rotate_surface_270
};

// rotate and copy src* surface to dst surface
void rotate_surface(GRSurface *dst, GRSurface *src)
{
    rotate_surface_t rotate;
    rotate=rotate_func[rotate_config(dst)];
    rotate(dst, src);
}
/* --- MTK rorate */

static gr_surface fbdev_init(minui_backend* backend) {
    int fd;
    void *bits;

    struct fb_fix_screeninfo fi;

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0) {
        perror("cannot open fb0");
        return NULL;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    // We print this out for informational purposes only, but
    // throughout we assume that the framebuffer device uses an RGBX
    // pixel format.  This is the case for every development device I
    // have access to.  For some of those devices (eg, hammerhead aka
    // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
    // different format (XBGR) but actually produces the correct
    // results on the display when you write RGBX.
    //
    // If you have a device that actually *needs* another pixel format
    // (ie, BGRX, or 565), patches welcome...

    printf("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n",
           vi.bits_per_pixel,
           vi.red.offset, vi.red.length,
           vi.green.offset, vi.green.length,
           vi.blue.offset, vi.blue.length);

    bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return NULL;
    }

    memset(bits, 0, fi.smem_len);

    gr_framebuffer[0].width = vi.xres;
    gr_framebuffer[0].height = vi.yres;
    gr_framebuffer[0].row_bytes = fi.line_length;
    gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
    gr_framebuffer[0].data = bits;
    memset(gr_framebuffer[0].data, 0, gr_framebuffer[0].height * gr_framebuffer[0].row_bytes);

    /* check if we can use double buffering */
    printf("[graphics] vi.yres * fi.line_length = %d * %d * 2 = %d, fi.smem_len=%d\n",
        vi.yres, fi.line_length, vi.yres * fi.line_length * 2, fi.smem_len);

    if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
        double_buffered = true;
        printf("[graphics] double buffered\n");

        memcpy(gr_framebuffer+1, gr_framebuffer, sizeof(GRSurface));
        gr_framebuffer[1].data = gr_framebuffer[0].data +
            gr_framebuffer[0].height * gr_framebuffer[0].row_bytes;

        gr_draw = gr_framebuffer+1;

    } else {
        double_buffered = false;
        printf("[graphics] without double buffer\n");
        // Without double-buffering, we allocate RAM for a buffer to
        // draw in, and then "flipping" the buffer consists of a
        // memcpy from the buffer we allocated to the framebuffer.

        gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
        memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
        gr_draw->data = (unsigned char*) malloc(gr_draw->height * gr_draw->row_bytes);
        if (!gr_draw->data) {
            perror("failed to allocate in-memory surface");
            return NULL;
        }
    }

    memset(gr_draw->data, 0, gr_draw->height * gr_draw->row_bytes);
    fb_fd = fd;
    set_displayed_framebuffer(0);

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    fbdev_blank(backend, true);
    fbdev_blank(backend, false);

    return rotate_canvas_get(gr_draw);
}

static gr_surface fbdev_flip(minui_backend* backend __unused) {
/* MTK rorate --- */
    rotate_surface(gr_draw, rotate_canvas_get(gr_draw));
/* --- MTK rorate */
    if (double_buffered) {
#if defined(RECOVERY_BGRA)
        // In case of BGRA, do some byte swapping
        unsigned int idx;
        unsigned char tmp;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
                idx += 4) {
            tmp = ucfb_vaddr[idx];
            ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
            ucfb_vaddr[idx + 2] = tmp;
        }
#endif
        // Change gr_draw to point to the buffer currently displayed,
        // then flip the driver so we're displaying the other buffer
        // instead.
        gr_draw = gr_framebuffer + displayed_buffer;
        set_displayed_framebuffer(1-displayed_buffer);
    } else {
        // Copy from the in-memory surface to the framebuffer.

#if defined(RECOVERY_BGRA)
        unsigned int idx;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_framebuffer[0].data;
        unsigned char* ucbuffer_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes); idx += 4) {
            ucfb_vaddr[idx    ] = ucbuffer_vaddr[idx + 2];
            ucfb_vaddr[idx + 1] = ucbuffer_vaddr[idx + 1];
            ucfb_vaddr[idx + 2] = ucbuffer_vaddr[idx    ];
            ucfb_vaddr[idx + 3] = ucbuffer_vaddr[idx + 3];
        }
#else
        memcpy(gr_framebuffer[0].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
#endif
    }
    return rotate_canvas_get(gr_draw);
}

static void fbdev_exit(minui_backend* backend __unused) {
    close(fb_fd);
    fb_fd = -1;
/* MTK rorate --- */
    rotate_canvas_exit();
/* --- MTK rorate */
    if (!double_buffered && gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
}
