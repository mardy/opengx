/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)
All rights reserved.

Attention! Contains pieces of code from others such as Mesa and GRRLib

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of copyright holders nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#include "pixels.h"

#include "debug.h"
#include <math.h>
#include <string.h>
#include <sys/param.h>

struct TexelRGBA8 {
    void store(void *texture, int16_t x, int16_t y, int16_t pitch) {
        int16_t block_x = x / 4;
        int16_t block_y = y / 4;
        uint8_t *d = static_cast<uint8_t*>(data) +
            block_y * pitch * 4 + block_x * 64 + (y % 4) * 8 + (x % 4) * 2;
        d[0] = a;
        d[1] = r;
        d[32] = g;
        d[33] = b;
    }

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct Texel16 {
    Texel16(uint8_t byte0, uint8_t byte1):

    void store(void *texture, int16_t x, int16_t y, int16_t pitch) {
        int16_t block_x = x / 4;
        int16_t block_y = y / 4;
        uint8_t *d = static_cast<uint8_t*>(data) +
            block_y * pitch * 4 + block_x * 32 + (y % 4) * 8 + (x % 4) * 2;
        d[0] = byte0();
        d[1] = byte1();
    }

    uint8_t byte0() const { return word >> 8; }
    uint8_t byte1() const { return word & 0xff; }

    uint16_t word;
}:

struct TexelIA8: public Texel16 {
    TexelIA8(uint8_t intensity, uint8_t alpha):
        Texel16 { (intensity << 8) | alpha } {}
};

struct TexelRGB565: public Texel16 {
    TexelIA8(uint8_t r, uint8_t g, uint8_t b):
        Texel16 {
            ((r & 0xf8) << 8) |
            ((g & 0xfc) << 3) |
            ((b & 0xf8) >> 3) } {}
};

template typename <T, P>
struct DataRGBA {
    void *read(T *data, P &pixel) {
    }
};

template typename <T>
struct DataReaderRGBA {
    DataReaderRGBA<T>(void *data, int width, int height, int alignment):
        m_data(data),
        m_width(width),
        m_height(height),
        m_alignment(alignment) {}

#define FLOAT_TO_BYTE(f) ((GLbyte)(f * 255.0) & 0xff)

static inline void *read_pixel_rgba32(void *data, GXColor *pixel)
{
    uint8_t *d = data;
    pixel->r = d[0];
    pixel->g = d[1];
    pixel->b = d[2];
    pixel->a = d[3];
    return d + 4;
}

static inline void *read_pixel_rgbaf(void *data, GXColor *pixel)
{
    float *d = data;
    pixel->r = FLOAT_TO_BYTE(d[0]);
    pixel->g = FLOAT_TO_BYTE(d[1]);
    pixel->b = FLOAT_TO_BYTE(d[2]);
    pixel->a = FLOAT_TO_BYTE(d[3]);
    return d + 4;
}

static inline void *read_pixel_rgbf(void *data, GXColor *pixel)
{
    float *d = data;
    pixel->r = FLOAT_TO_BYTE(d[0]);
    pixel->g = FLOAT_TO_BYTE(d[1]);
    pixel->b = FLOAT_TO_BYTE(d[2]);
    return d + 3;
}

/* luminance alpha */
static inline void *read_pixel_laf(void *data, GXColor *pixel)
{
    float *d = data;
    pixel->r = FLOAT_TO_BYTE(d[0]);
    pixel->a = FLOAT_TO_BYTE(d[1]);
    return d + 2;
}

static inline uint16_t *address_for_texel_16bit(void *data, int x, int y, pitch)
{
    int block_x = x / 4;
    int block_y = y / 4;
    return data + (block_y * pitch * 4 + block_x * 32 +
                   (y % 4) * 8 + (x % 4) * 2);
}

static inline uint16_t *address_for_texel_32bit(void *data, int x, int y, pitch)
{
    int block_x = x / 4;
    int block_y = y / 4;
    return data + (block_y * pitch * 4 + block_x * 64 +
                   (y % 4) * 8 + (x % 4) * 2);
}

static inline void write_pixel_gxrgb565(void *data, int x, int y, int pitch,
                                        GXColor *pixel)
{
    uint16_t *d = address_for_texel_16bit(data, x, y, pitch);
    *d = ((pixel->r & 0xf8) << 8) |
         ((pixel->g & 0xfc) << 3) |
         ((pixel->b & 0xf8) >> 3);
}

static inline void write_pixel_gxrgba32(void *data, int x, int y, int pitch,
                                        GXColor *pixel)
{
    uint16_t *d = address_for_texel_32bit(data, x, y, pitch);
    *d = (pixel->a << 8) | pixel->r;
    *(d + 16) = (pixel->g << 8) | pixel->b;
}

#define DEFINE_CONVERSION_FUNCTION(fromfmt, tofmt) \
    void _ogx_conv_ ## fromfmt ## _to_ ## tofmt( \
        void *src, int width, int height, int srcpitch, \
        void *dest, int x, int y, int dstpitch) \
    { \
        for (int ry = 0; ry < height; ry++) { \
            void *srcline = src + (ry * srcpitch); \
            for (int rx = 0; rx < width; rx++) { \
                GXColor c; \
                srcline = read_pixel_ ## fromfmt(srcline, &c); \
                write_pixel_ ## tofmt(dest, rx + x, ry + y, dstpitch, &c); \
            } \
        } \
    }

DEFINE_CONVERSION_FUNCTION(rgbaf, gxrgb565)

void _ogx_swap_rgba(unsigned char *pixels, int num_pixels)
{
    while (num_pixels--) {
        unsigned char temp;
        temp = pixels[0];
        pixels[0] = pixels[3];
        pixels[3] = temp;
        pixels += 4;
    }
}

void _ogx_swap_rgb565(unsigned short *pixels, int num_pixels)
{
    while (num_pixels--) {
        unsigned int b = *pixels & 0x1F;
        unsigned int r = (*pixels >> 11) & 0x1F;
        unsigned int g = (*pixels >> 5) & 0x3F;
        *pixels++ = (b << 11) | (g << 5) | r;
    }
}

static void conv_rgba32_to_rgb565(const int8_t *src, void *dst, int numpixels)
{
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | ((src[2] >> 3));
        src += 4;
    }
}

static void conv_rgbaf_to_rgb565(const float *src, void *dst, int numpixels)
{
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((FLOAT_TO_BYTE(src[0]) & 0xF8) << 8) |
            ((FLOAT_TO_BYTE(src[1]) & 0xFC) << 3) |
            ((FLOAT_TO_BYTE(src[2]) >> 3));
        src += 4;
    }
}

// Discards alpha and fits the texture in 16 bits
void _ogx_conv_rgba_to_rgb565(const void *data, GLenum type,
                              void *dst, int width, int height)
{
    int numpixels = width * height;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        conv_rgba32_to_rgb565(data, dst, numpixels);
        break;
    case GL_FLOAT:
        conv_rgbaf_to_rgb565(data, dst, numpixels);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}

static void conv_rgb24_to_rgb565(const uint8_t *src, void *dst, int numpixels)
{
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | ((src[2] >> 3));
        src += 3;
    }
}

static void conv_rgbf_to_rgb565(const float *src, void *dst, int numpixels)
{
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((FLOAT_TO_BYTE(src[0]) & 0xF8) << 8) |
            ((FLOAT_TO_BYTE(src[1]) & 0xFC) << 3) |
            ((FLOAT_TO_BYTE(src[2]) >> 3));
        src += 3;
    }
}

// Fits the texture in 16 bits
void _ogx_conv_rgb_to_rgb565(const void *data, GLenum type,
                             void *dst, int width, int height)
{
    int numpixels = width * height;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        conv_rgb24_to_rgb565(data, dst, numpixels);
        break;
    case GL_FLOAT:
        conv_rgbf_to_rgb565(data, dst, numpixels);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}

static void conv_rgbaf_to_rgba32(const float *src, void *dst, int numpixels)
{
    uint32_t *out = dst;
    while (numpixels--) {
        *out++ = (FLOAT_TO_BYTE(src[0]) << 24) |
            (FLOAT_TO_BYTE(src[1]) << 16) |
            (FLOAT_TO_BYTE(src[2]) << 8) |
            FLOAT_TO_BYTE(src[3]);
        src += 4;
    }
}

void _ogx_conv_rgba_to_rgba32(const void *data, GLenum type,
                              void *dest, int width, int height)
{
    int numpixels = width * height;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        memcpy(dest, data, numpixels * 4);
        break;
    case GL_FLOAT:
        conv_rgbaf_to_rgba32(data, dest, numpixels);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}

static void conv_intensityf_to_i8(const float *src, void *dst, int numpixels)
{
    uint32_t *out = dst;
    while (numpixels--) {
        *out++ = FLOAT_TO_BYTE(src[0]);
        src++;
    }
}

void _ogx_conv_intensity_to_i8(const void *data, GLenum type,
                               void *dest, int width, int height)
{
    int numpixels = width * height;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        memcpy(dest, data, numpixels);
        break;
    case GL_FLOAT:
        conv_intensityf_to_i8(data, dest, numpixels);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}

static void conv_laf_to_ia8(const float *src, void *dst, int numpixels)
{
    uint16_t *out = dst;
    while (numpixels--) {
        *out++ = (FLOAT_TO_BYTE(src[1]) << 8) | FLOAT_TO_BYTE(src[0]);
        src += 2;
    }
}

void _ogx_conv_luminance_alpha_to_ia8(const void *data, GLenum type,
                                      void *dest, int width, int height)
{
    int numpixels = width * height;
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        memcpy(dest, data, numpixels * 2);
        break;
    case GL_FLOAT:
        conv_laf_to_ia8(data, dest, numpixels);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}

// Converts color into luminance and saves alpha
void _ogx_conv_rgba_to_luminance_alpha(unsigned char *src, void *dst,
                                       const unsigned int width, const unsigned int height)
{
    int numpixels = width * height;
    unsigned char *out = dst;
    while (numpixels--) {
        int lum = ((int)src[0]) + ((int)src[1]) + ((int)src[2]);
        lum = lum / 3;
        *out++ = src[3];
        *out++ = lum;
        src += 4;
    }
}

// 4x4 tile scrambling
/* 1b texel scrambling */
void _ogx_scramble_1b(void *src, void *dst, int width, int height)
{
    uint64_t *s = src;
    uint64_t *p = dst;

    int width_blocks = (width + 7) / 8;
    for (int y = 0; y < height; y += 4) {
        int rows = MIN(height - y, 4);
        for (int x = 0; x < width_blocks; x++) {
            for (int row = 0; row < rows; row++) {
                *p++ = s[(y + row) * width_blocks + x];
            }
        }
    }
}

// 2b texel scrambling
void _ogx_scramble_2b(unsigned short *src, void *dst,
                      const unsigned int width, const unsigned int height)
{
    unsigned int block;
    unsigned int i;
    unsigned char c;
    unsigned short *p = (unsigned short *)dst;

    for (block = 0; block < height; block += 4) {
        for (i = 0; i < width; i += 4) {
            for (c = 0; c < 4; c++) {
                *p++ = src[(block + c) * width + i];
                *p++ = src[(block + c) * width + i + 1];
                *p++ = src[(block + c) * width + i + 2];
                *p++ = src[(block + c) * width + i + 3];
            }
        }
    }
}

// 4b texel scrambling
void _ogx_scramble_4b(unsigned char *src, void *dst,
                      const unsigned int width, const unsigned int height)
{
    unsigned int block;
    unsigned int i;
    unsigned char c;
    unsigned char argb;
    unsigned char *p = (unsigned char *)dst;

    for (block = 0; block < height; block += 4) {
        for (i = 0; i < width; i += 4) {
            for (c = 0; c < 4; c++) {
                for (argb = 0; argb < 4; argb++) {
                    *p++ = src[((i + argb) + ((block + c) * width)) * 4 + 3];
                    *p++ = src[((i + argb) + ((block + c) * width)) * 4];
                }
            }
            for (c = 0; c < 4; c++) {
                for (argb = 0; argb < 4; argb++) {
                    *p++ = src[(((i + argb) + ((block + c) * width)) * 4) + 1];
                    *p++ = src[(((i + argb) + ((block + c) * width)) * 4) + 2];
                }
            }
        }
    }
}

//// Image scaling for arbitrary size taken from Mesa 3D and adapted by davidgf ////

void _ogx_scale_internal(int components, int widthin, int heightin,
                         const unsigned char *datain,
                         int widthout, int heightout, unsigned char *dataout)
{
    float x, lowx, highx, convx, halfconvx;
    float y, lowy, highy, convy, halfconvy;
    float xpercent, ypercent;
    float percent;
    /* Max components in a format is 4, so... */
    float totals[4];
    float area;
    int i, j, k, yint, xint, xindex, yindex;
    int temp;

    convy = (float)heightin / heightout;
    convx = (float)widthin / widthout;
    halfconvx = convx / 2;
    halfconvy = convy / 2;
    for (i = 0; i < heightout; i++) {
        y = convy * (i + 0.5);
        if (heightin > heightout) {
            highy = y + halfconvy;
            lowy = y - halfconvy;
        } else {
            highy = y + 0.5;
            lowy = y - 0.5;
        }
        for (j = 0; j < widthout; j++) {
            x = convx * (j + 0.5);
            if (widthin > widthout) {
                highx = x + halfconvx;
                lowx = x - halfconvx;
            } else {
                highx = x + 0.5;
                lowx = x - 0.5;
            }

            /*
            ** Ok, now apply box filter to box that goes from (lowx, lowy)
            ** to (highx, highy) on input data into this pixel on output
            ** data.
            */
            totals[0] = totals[1] = totals[2] = totals[3] = 0.0;
            area = 0.0;

            y = lowy;
            yint = floor(y);
            while (y < highy) {
                yindex = (yint + heightin) % heightin;
                if (highy < yint + 1) {
                    ypercent = highy - y;
                } else {
                    ypercent = yint + 1 - y;
                }

                x = lowx;
                xint = floor(x);

                while (x < highx) {
                    xindex = (xint + widthin) % widthin;
                    if (highx < xint + 1) {
                        xpercent = highx - x;
                    } else {
                        xpercent = xint + 1 - x;
                    }

                    percent = xpercent * ypercent;
                    area += percent;
                    temp = (xindex + (yindex * widthin)) * components;
                    for (k = 0; k < components; k++) {
                        totals[k] += datain[temp + k] * percent;
                    }

                    xint++;
                    x = xint;
                }
                yint++;
                y = yint;
            }

            temp = (j + (i * widthout)) * components;
            for (k = 0; k < components; k++) {
                /* totals[] should be rounded in the case of enlarging an RGB
                 * ramp when the type is 332 or 4444
                 */
                dataout[temp + k] = (totals[k] + 0.5) / area;
            }
        }
    }
}
