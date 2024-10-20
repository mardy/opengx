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
#include "opengx.h"
#include "pixel_stream.h"
#include "state.h"
#include "texel.h"

#include <math.h>
#include <ogc/gx.h>
#include <variant>

#define MAX_FAST_CONVERSIONS 8

typedef void (FastConverter)(const void *data, GLenum type, int width, int height,
                              void *dst, int x, int y, int dstpitch);
static struct FastConversion {
    GLenum gl_format;
    u8 gx_format;
    union {
        uintptr_t id;
        FastConverter *func;
    } conv;
} s_registered_conversions[MAX_FAST_CONVERSIONS] = {
    { GL_RGB, GX_TF_RGB565, ogx_fast_conv_RGB_RGB565 },
    { GL_RGBA, GX_TF_RGBA8, ogx_fast_conv_RGBA_RGBA8 },
    { GL_LUMINANCE, GX_TF_I8, ogx_fast_conv_Intensity_I8 },
    0,
};

/* This template class is used to perform reading of a pixel and storing it in
 * the desired texture format in a single go. It does that in about 1/5th of
 * the time the generic algorithm (on Dolphin the difference is even bigger, up
 * to 1/10th), at the expense of a larger code size.
 *
 * Note that this class does not support packed pixel formats: each pixel
 * component must be at least one byte wide.
 */
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
            pixel.set_color(component(data[0]),
                            component(data[1]),
                            component(data[2]),
                            component(data[3]));
        } else if constexpr (NUM_ELEMS >= 3 && P::has_rgb && !P::has_alpha) {
            pixel.set_color(component(data[0]),
                            component(data[1]),
                            component(data[2]));
        } else if constexpr (NUM_ELEMS == 3 && P::has_rgb && P::has_alpha) {
            pixel.set_color(component(data[0]),
                            component(data[1]),
                            component(data[2]),
                            1.0f);
        } else {
            /* TODO (maybe) support converting from intensity to RGB */
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
                pixel.set_luminance_alpha(luminance, alpha);
            } else if constexpr (P::has_luminance) {
                pixel.set_luminance(luminance);
            } else { /* Only alpha */
                pixel.set_alpha(alpha);
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

template <typename T>
using DataReaderAlpha = DataReader<T, 1, GL_ALPHA>;

template <typename READER, typename TEXEL> static inline
void load_texture_typed(const void *src, int width, int height,
                        void *dest, int x, int y, int dstpitch)
{
    // TODO: add alignment options
    using DataType = typename READER::type;

    int row_length = glparamstate.unpack_row_length > 0 ?
        glparamstate.unpack_row_length : width;
    int srcpitch = READER::pitch_for_width(row_length);

    TEXEL texel;
    texel.set_area(dest, x, y, width, height, dstpitch);
    for (int ry = 0; ry < height; ry++) {
        const DataType *srcline = READER::row_ptr(src, ry, srcpitch);
        for (int rx = 0; rx < width; rx++) {
            srcline = READER::read(srcline, texel);
            texel.store();
        }
    }
}

template <template<typename> typename READERBASE, typename TEXEL> static inline
void load_texture(const void *data, GLenum type, int width, int height,
                  void *dst, int x, int y, int dstpitch)
{
    using Texel = TEXEL;

    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        load_texture_typed<READERBASE<uint8_t>,Texel>(data, width, height,
                                                      dst, x, y, dstpitch);
        break;
    case GL_FLOAT:
        load_texture_typed<READERBASE<float>,Texel>(data, width, height,
                                                    dst, x, y, dstpitch);
        break;
    default:
        warning("Unsupported texture format %04x", type);
    }
}

static int get_pixel_size_in_bits(GLenum format, GLenum type)
{
    int type_size = 0;
    switch (type) {
    case GL_UNSIGNED_BYTE:
        type_size = sizeof(GLbyte); break;
    case GL_UNSIGNED_SHORT:
        type_size = sizeof(GLshort); break;
    case GL_UNSIGNED_INT:
        type_size = sizeof(GLint); break;
    case GL_FLOAT:
        type_size = sizeof(GLfloat); break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        {
            const MasksPerType *mask =
                CompoundPixelStream::find_mask_per_type(type);
            return mask->bytes * 8;
        }
    case GL_BITMAP:
        return 1;
    default:
        warning("Unknown texture data type %x\n", type);
    }

    const ComponentsPerFormat *c =
        GenericPixelStream<uint8_t>::find_component_data(format);
    if (!c) {
        warning("Unknown texture format %x\n", format);
        return 0;
    }

    return c->components_per_pixel * type_size * 8;
}

void _ogx_bytes_to_texture(const void *data, GLenum format, GLenum type,
                           int width, int height,
                           void *dst, uint32_t gx_format,
                           int x, int y, int dstpitch)
{
    /* Skip degenerate cases */
    if (width <= 0 || height <= 0) return;

    /* The GL_UNPACK_SKIP_ROWS and GL_UNPACK_SKIP_PIXELS can be handled here by
     * modifiying the source data pointer. */
    bool need_skip_pixels = false;
    int row_length = glparamstate.unpack_row_length > 0 ?
        glparamstate.unpack_row_length : width;
    if (glparamstate.unpack_skip_pixels > 0 || glparamstate.unpack_skip_rows > 0) {
        int pixel_size_bits = get_pixel_size_in_bits(format, type);
        int row_size_bytes = (row_length * pixel_size_bits + 7) / 8;
        /* For bitmaps, the skip_pixels case is handled in the reader itself,
         * since we cannot skip partial bytes here. */
        int skip_pixels = 0;
        if (pixel_size_bits >= 8) {
            skip_pixels = glparamstate.unpack_skip_pixels * pixel_size_bits;
        } else {
            need_skip_pixels = true;
        }
        data = static_cast<const uint8_t*>(data) + skip_pixels +
            glparamstate.unpack_skip_rows * row_size_bytes;
    }
    /* Accelerate the most common transformations by using the specialized
     * readers. We only do this for some transformations, since every
     * instantiation of the template takes some space, and the number of
     * possible combinations is polynomial.
     */
    if (type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_FLOAT) {
        for (int i = 0; i < MAX_FAST_CONVERSIONS; i++) {
            const FastConversion &c = s_registered_conversions[i];
            if (c.gl_format == 0) break;

            if (c.gl_format == format && c.gx_format == gx_format) {
                c.conv.func(data, type, width, height, dst, x, y, dstpitch);
                return;
            }
        }
    }

    debug(OGX_LOG_TEXTURE,
          "No fast conversion registered for GL format %04x to GX format %d",
          format, gx_format);

    /* Here starts the code for the generic converter. We start by selecting
     * the proper Texel subclass for the given GX texture format, then we
     * select the reader based on the GL type parameter, and then we do the
     * conversion pixel by pixel, by using GXColor as intermediate format.
     *
     * We use std::variant so that we can safely construct our objects on the
     * stack. */
    std::variant<
        TexelRGBA8,
        TexelRGB565,
        TexelIA8,
        TexelI8,
        TexelI4,
        TexelA8
    > texel_v;
    Texel *texel;

    std::variant<
        BitmapPixelStream,
        CompoundPixelStream,
        GenericPixelStream<uint8_t>,
        GenericPixelStream<uint16_t>,
        GenericPixelStream<uint32_t>,
        GenericPixelStream<float>
    > reader_v;
    PixelStreamBase *reader;

    switch (gx_format) {
    case GX_TF_RGBA8:
        texel_v = TexelRGBA8();
        texel = &std::get<TexelRGBA8>(texel_v);
        break;
    case GX_TF_RGB565:
        texel_v = TexelRGB565();
        texel = &std::get<TexelRGB565>(texel_v);
        break;
    case GX_TF_IA8:
        texel_v = TexelIA8();
        texel = &std::get<TexelIA8>(texel_v);
        break;
    case GX_TF_I8:
        texel_v = TexelI8();
        texel = &std::get<TexelI8>(texel_v);
        break;
    case GX_TF_A8:
        texel_v = TexelA8();
        texel = &std::get<TexelA8>(texel_v);
        break;
    case GX_TF_I4:
        texel_v = TexelI4();
        texel = &std::get<TexelI4>(texel_v);
        break;
    }

    switch (type) {
    case GL_UNSIGNED_BYTE:
        reader_v = GenericPixelStream<uint8_t>(format, type);
        reader = &std::get<GenericPixelStream<uint8_t>>(reader_v);
        break;
    case GL_UNSIGNED_SHORT:
        reader_v = GenericPixelStream<uint16_t>(format, type);
        reader = &std::get<GenericPixelStream<uint16_t>>(reader_v);
        break;
    case GL_UNSIGNED_INT:
        reader_v = GenericPixelStream<uint32_t>(format, type);
        reader = &std::get<GenericPixelStream<uint32_t>>(reader_v);
        break;
    case GL_FLOAT:
        reader_v = GenericPixelStream<float>(format, type);
        reader = &std::get<GenericPixelStream<float>>(reader_v);
        break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        reader_v = CompoundPixelStream(format, type);
        reader = &std::get<CompoundPixelStream>(reader_v);
        break;
    case GL_BITMAP:
        reader_v = BitmapPixelStream();
        reader = &std::get<BitmapPixelStream>(reader_v);
        break;
    default:
        warning("Unknown texture data type %x\n", type);
    }

    int skip_pixels_after = 0;
    if (need_skip_pixels) {
        for (int i = 0; i < glparamstate.unpack_skip_pixels; i++) {
            reader->read();
        }
        skip_pixels_after = row_length - width;
    }

    reader->setup_stream(data, width, height);
    texel->set_area(dst, x, y, width, height, dstpitch);
    for (int ry = 0; ry < height; ry++) {
        if (ry > 0) {
            for (int i = 0; i < skip_pixels_after; i++) {
                reader->read();
            }
        }
        for (int rx = 0; rx < width; rx++) {
            GXColor c = reader->read();
            texel->set_color(c);
            texel->store();
        }
    }
}

int _ogx_pitch_for_width(uint32_t gx_format, int width)
{
    switch (gx_format) {
    case GX_TF_RGBA8:
        return TexelRGBA8::compute_pitch(width);
    case GX_TF_RGB565:
    case GX_TF_IA8:
        return TexelRGB565::compute_pitch(width);
    case GX_TF_I8:
    case GX_TF_A8:
        return TexelI8::compute_pitch(width);
    case GX_TF_I4:
        return TexelI4::compute_pitch(width);
    default:
        return -1;
    }
}

uint8_t _ogx_gl_format_to_gx(GLenum format)
{
    switch (format) {
    case 3:
    case GL_RGB:
    case GL_BGR:
    case GL_RGB4:
    case GL_RGB5:
    case GL_RGB8:
        return GX_TF_RGB565;
    case 4:
    case GL_RGBA:
    case GL_BGRA:
    case GL_COMPRESSED_RGBA_ARB: /* No support for compressed alpha textures */
        return GX_TF_RGBA8;
    case GL_LUMINANCE_ALPHA: return GX_TF_IA8;
    case GL_LUMINANCE: return GX_TF_I8;
    case GL_ALPHA:
        /* Note, we won't be really passing this to GX */
        return GX_TF_A8;
    default:
        return GX_TF_CMPR;
    }
}

uint8_t _ogx_find_best_gx_format(GLenum format, GLenum internal_format,
                                 int width, int height)
{
    // Simplify and avoid stupid conversions (which waste space for no gain)
    if (format == GL_RGB && internal_format == GL_RGBA)
        internal_format = GL_RGB;

    if (format == GL_LUMINANCE_ALPHA && internal_format == GL_RGBA)
        internal_format = GL_LUMINANCE_ALPHA;

    uint8_t gx_format = _ogx_gl_format_to_gx(internal_format);
    if (gx_format == GX_TF_CMPR && (width < 8 || height < 8)) {
        // Cannot take compressed textures under 8x8 (4 blocks of 4x4, 32B)
        gx_format = GX_TF_RGB565;
    }
    return gx_format;
}

#define DEFINE_FAST_CONVERSION(reader, texel) \
    static void fast_conv_##reader##_##texel( \
        const void *data, GLenum type, int width, int height, \
        void *dst, int x, int y, int dstpitch) \
    { \
        load_texture<DataReader ## reader, Texel ## texel>( \
            data, type, width, height, dst, x, y, dstpitch); \
    } \
    uintptr_t ogx_fast_conv_##reader##_##texel = (uintptr_t)fast_conv_##reader##_##texel;

/* Fast conversions marked by a star are enabled by default */
DEFINE_FAST_CONVERSION(RGBA, I8)
DEFINE_FAST_CONVERSION(RGBA, A8)
DEFINE_FAST_CONVERSION(RGBA, IA8)
DEFINE_FAST_CONVERSION(RGBA, RGB565)
DEFINE_FAST_CONVERSION(RGBA, RGBA8) // *
DEFINE_FAST_CONVERSION(RGB, I8)
DEFINE_FAST_CONVERSION(RGB, IA8)
DEFINE_FAST_CONVERSION(RGB, RGB565) // *
DEFINE_FAST_CONVERSION(RGB, RGBA8)
DEFINE_FAST_CONVERSION(LA, I8)
DEFINE_FAST_CONVERSION(LA, A8)
DEFINE_FAST_CONVERSION(LA, IA8)
DEFINE_FAST_CONVERSION(Intensity, I8) // *
DEFINE_FAST_CONVERSION(Alpha, A8)

void ogx_register_tex_conversion(GLenum format, GLenum internal_format,
                                 uintptr_t converter)
{
    uint8_t gx_format = _ogx_gl_format_to_gx(internal_format);
    int i;
    for (i = 0; i < MAX_FAST_CONVERSIONS; i++) {
        if (s_registered_conversions[i].gl_format == 0) break;
    }

    if (i >= MAX_FAST_CONVERSIONS) {
        /* Nothing especially bad happens, we'll just use the slower
         * conversion. But print a warning in any case. */
        warning("ogx_register_tex_conversion: reached max num of entries");
        return;
    }

    FastConversion &c = s_registered_conversions[i];
    c.gl_format = format;
    c.gx_format = gx_format;
    c.conv.id = converter;
}
