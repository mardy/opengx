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

static inline uint8_t luminance_from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* TODO: fix this, the three components do not have the same weight to the
     * human eye. Use a formula from
     * https://songho.ca/dsp/luminance/luminance.html
     */
    int luminance = int(r) + int(g) + int(b);
    return luminance / 3;
}

struct TexelRGBA8 {
    static constexpr bool has_rgb = true;
    static constexpr bool has_alpha = true;
    static constexpr bool has_luminance = false;

    TexelRGBA8() = default;
    TexelRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a):
        r(r), g(g), b(b), a(a) {}

    void store(void *texture, int x, int y, int pitch) {
        int block_x = x / 4;
        int block_y = y / 4;
        uint8_t *d = static_cast<uint8_t*>(texture) +
            block_y * pitch * 4 + block_x * 64 + (y % 4) * 8 + (x % 4) * 2;
        d[0] = a;
        d[1] = r;
        d[32] = g;
        d[33] = b;
    }

    static inline int compute_pitch(int width) {
        /* texel are in pairs of 4x4 blocks, each element 2 bytes wide */
        return ((width + 3) / 4) * 16;
    }

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct Texel16 {
    void store(void *texture, int x, int y, int pitch) {
        int block_x = x / 4;
        int block_y = y / 4;
        uint8_t *d = static_cast<uint8_t*>(texture) +
            block_y * pitch * 4 + block_x * 32 + (y % 4) * 8 + (x % 4) * 2;
        d[0] = byte0();
        d[1] = byte1();
    }

    static inline int compute_pitch(int width) {
        /* texel are in 4x4 blocks, each element 2 bytes wide */
        return ((width + 3) / 4) * 8;
    }

    uint8_t byte0() const { return word >> 8; }
    uint8_t byte1() const { return word & 0xff; }

    uint16_t word;
};

struct TexelIA8: public Texel16 {
    static constexpr bool has_rgb = false;
    static constexpr bool has_alpha = true;
    static constexpr bool has_luminance = true;

    TexelIA8() = default;
    TexelIA8(uint8_t intensity, uint8_t alpha):
        Texel16 { uint16_t((alpha << 8) | intensity) } {}
};

struct TexelRGB565: public Texel16 {
    static constexpr bool has_alpha = false;
    static constexpr bool has_rgb = true;

    TexelRGB565() = default;
    TexelRGB565(uint8_t r, uint8_t g, uint8_t b):
        Texel16 { uint16_t(
            ((r & 0xf8) << 8) |
            ((g & 0xfc) << 3) |
            ((b & 0xf8) >> 3)) } {}
};

struct TexelI8 {
    static constexpr bool has_rgb = false;
    static constexpr bool has_alpha = false;
    static constexpr bool has_luminance = true;

    TexelI8() = default;
    TexelI8(uint8_t luminance):
        luminance(luminance) {}

    void store(void *texture, int x, int y, int pitch) {
        int block_x = x / 8;
        int block_y = y / 4;
        uint8_t *d = static_cast<uint8_t*>(texture) +
            block_y * pitch * 4 + block_x * 32 + (y % 4) * 8 + (x % 8);
        d[0] = luminance;
    }

    static inline int compute_pitch(int width) {
        /* texel are in 8x4 blocks, each element 1 bytes wide */
        return ((width + 7) / 8) * 8;
    }

    uint8_t luminance;
};

template <typename T> static inline uint8_t component(T value);
template <> inline uint8_t component(uint8_t value) { return value; }
template <> inline uint8_t component(float value) {
    return (uint8_t)(int(value * 255.0f) & 0xff);
}

template <typename T, char NUM_ELEMS, GLenum FORMAT>
struct DataReader {
    typedef T type;

    static inline int pitch_for_width(int width) {
        return width * NUM_ELEMS * sizeof(T);
    }

    static inline const T *row_ptr(const void *data, int y, int pitch) {
        return static_cast<const T *>(data) + y * pitch / sizeof(T);
    }

    template <typename P>
    static inline const T *read(const T *data, P &pixel) {
        if constexpr (NUM_ELEMS == 4 && P::has_rgb && P::has_alpha) {
            pixel = P(component(data[0]),
                      component(data[1]),
                      component(data[2]),
                      component(data[3]));
        } else if constexpr (NUM_ELEMS >= 3 && P::has_rgb) {
            pixel = P(component(data[0]),
                      component(data[1]),
                      component(data[2]));
        } else {
            uint8_t luminance, alpha;
            if constexpr (P::has_luminance) {
                if constexpr (NUM_ELEMS >= 3) {
                    luminance = luminance_from_rgb(component(data[0]),
                                                   component(data[1]),
                                                   component(data[2]));
                } else if constexpr (NUM_ELEMS == 2) {
                    luminance = component(data[0]);
                } else { // Just a single component in the source data
                    if constexpr (FORMAT == GL_LUMINANCE) {
                        luminance = component(data[0]);
                    } else {
                        luminance = 0;
                    }
                }
            }

            if constexpr (P::has_alpha) {
                if constexpr (NUM_ELEMS == 4) {
                    alpha = component(data[3]);
                } else if constexpr (FORMAT == GL_LUMINANCE_ALPHA) {
                    alpha = component(data[1]);
                } else if constexpr (FORMAT == GL_ALPHA) {
                    alpha = component(data[0]);
                } else {
                    alpha = 255;
                }
            }

            if constexpr (P::has_luminance && P::has_alpha) {
                pixel = P(luminance, alpha);
            } else if constexpr (P::has_luminance) {
                pixel = P(luminance);
            } else { /* Only alpha */
                pixel = P(alpha);
            }
        }

        return data + NUM_ELEMS;
    }
};

template <typename T>
using DataReaderRGBA = DataReader<T, 4, GL_RGBA>;

template <typename T>
using DataReaderRGB = DataReader<T, 3, GL_RGB>;

template <typename T>
using DataReaderLA = DataReader<T, 2, GL_LUMINANCE_ALPHA>;

template <typename T>
using DataReaderIntensity = DataReader<T, 1, GL_LUMINANCE>;

template <typename READER, typename TEXEL> static inline
void load_texture_typed(const void *src, int width, int height,
                        void *dest, int x, int y, int dstpitch)
{
    // TODO: add alignment options
    using DataType = typename READER::type;

    int srcpitch = READER::pitch_for_width(width);

    for (int ry = 0; ry < height; ry++) {
        const DataType *srcline = READER::row_ptr(src, ry, srcpitch);
        for (int rx = 0; rx < width; rx++) {
            TEXEL texel;
            srcline = READER::read(srcline, texel);
            texel.store(dest, rx + x, ry + y, dstpitch);
        }
    }
}

template <template<typename> typename READERBASE, typename TEXEL> static inline
void load_texture(const void *data, GLenum type,
                   void *dst, int width, int height)
{
    using Texel = TEXEL;
    int dstpitch = Texel::compute_pitch(width);

    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        load_texture_typed<READERBASE<uint8_t>,Texel>(data, width, height,
                                                      dst, 0, 0, dstpitch);
        break;
    case GL_FLOAT:
        load_texture_typed<READERBASE<float>,Texel>(data, width, height,
                                                    dst, 0, 0, dstpitch);
        break;
    default:
        warning("Unsupported texture format %d", type);
    }
}


// Discards alpha and fits the texture in 16 bits
void _ogx_conv_rgba_to_rgb565(const void *data, GLenum type,
                              void *dst, int width, int height)
{
    load_texture<DataReaderRGBA, TexelRGB565>(data, type, dst, width, height);
}

// Fits the texture in 16 bits
void _ogx_conv_rgb_to_rgb565(const void *data, GLenum type,
                             void *dst, int width, int height)
{
    load_texture<DataReaderRGB, TexelRGB565>(data, type, dst, width, height);
}

void _ogx_conv_rgba_to_rgba32(const void *data, GLenum type,
                              void *dst, int width, int height)
{
    load_texture<DataReaderRGBA, TexelRGBA8>(data, type, dst, width, height);
}

void _ogx_conv_intensity_to_i8(const void *data, GLenum type,
                               void *dst, int width, int height)
{
    load_texture<DataReaderIntensity, TexelI8>(data, type, dst, width, height);
}

void _ogx_conv_luminance_alpha_to_ia8(const void *data, GLenum type,
                                      void *dst, int width, int height)
{
    load_texture<DataReaderLA, TexelIA8>(data, type, dst, width, height);
}

// Converts color into luminance and saves alpha
void _ogx_conv_rgba_to_luminance_alpha(const void *data, GLenum type,
                                       void *dst, int width, int height)
{
    load_texture<DataReaderRGBA, TexelIA8>(data, type, dst, width, height);
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
