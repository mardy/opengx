/*****************************************************************************
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

#ifndef OPENGX_PIXEL_STREAM_H
#define OPENGX_PIXEL_STREAM_H

#include <GL/gl.h>
#include <algorithm>
#include <ogc/gx.h>

#include "state.h"

extern const struct ComponentsPerFormat {
    GLenum format;
    char components_per_pixel;
    char component_index[4]; /* component role (0=red, ..., 3=alpha) */
} _ogx_pixels_components_per_format[];

extern const struct MasksPerType {
    GLenum type;
    char bytes; /* number of bytes per pixel */
    char rbits; /* bits of data for each component */
    char gbits;
    char bbits;
    char abits;
    char roff; /* offsets (relative to memory layout, not registers */
    char goff;
    char boff;
    char aoff;
} _ogx_pixels_masks_per_type[];

/* Base class for the generic reader: this is used as base class by the
 * CompoundPixelStream and the GenericPixelStream classes below.
 *
 * Note that for the time being we assume the pitch to be the minimum required
 * to store a row of pixels.
 */
struct PixelStreamBase {
    void setup_stream(void *data, int width, int height) {
        m_width = width;
        m_height = height;
        m_data = data;
        // TODO: add handling of row width and row alignment (to all readers!)
    }
    void setup_stream(const void *data, int width, int height) {
        setup_stream(const_cast<void*>(data), width, height);
    }

    virtual GXColor read() = 0;
    virtual void write(GXColor color) = 0;

protected:
    void *m_data;
    int m_width;
    int m_height;
};

template <typename T> static inline uint8_t component(T value);
template <> inline uint8_t component(uint8_t value) { return value; }
template <> inline uint8_t component(uint16_t value) { return value >> 8; }
template <> inline uint8_t component(uint32_t value) { return value >> 24; }
template <> inline uint8_t component(float value) {
    return (uint8_t)(int(std::clamp(value, 0.0f, 1.0f) * 255.0f) & 0xff);
}

template <typename T> static inline T glcomponent(uint8_t component);
template <> inline uint8_t glcomponent(uint8_t value) { return value; }
template <> inline uint16_t glcomponent(uint8_t value) {
    return (value << 8) | value;
}
template <> inline uint32_t glcomponent(uint8_t value) {
    return (value << 24) | (value << 16) | value;
}
template <> inline float glcomponent(uint8_t value) { return value / 255.0f; }

/* This class handles reading of pixels stored in one of the formats listed
 * above, where each pixel is packed in at most 32 bits. */
struct CompoundPixelStream: public PixelStreamBase {
    CompoundPixelStream() = default;
    CompoundPixelStream(GLenum format, GLenum type):
        format(format),
        mask_data(*find_mask_per_type(type)) {
        if (format == GL_BGR || format == GL_BGRA) { /* swap red and blue */
            char tmp = mask_data.roff;
            mask_data.roff = mask_data.boff;
            mask_data.boff = tmp;
        }
        rmask = compute_mask(mask_data.rbits, mask_data.roff);
        gmask = compute_mask(mask_data.gbits, mask_data.goff);
        bmask = compute_mask(mask_data.bbits, mask_data.boff);
        amask = compute_mask(mask_data.abits, mask_data.aoff);
    }

    static const MasksPerType *find_mask_per_type(GLenum type) {
        for (int i = 0; _ogx_pixels_masks_per_type[i].type != 0; i++) {
            if (_ogx_pixels_masks_per_type[i].type == type) {
                return &_ogx_pixels_masks_per_type[i];
            }
        }
        return nullptr;
    }

    inline uint32_t compute_mask(int nbits, int offset) {
        uint32_t mask = (1 << nbits) - 1;
        return mask << (mask_data.bytes * 8 - (nbits + offset));
    }

    inline uint8_t read_component(uint32_t pixel, uint32_t mask,
                                  int nbits, int offset) {
        uint32_t value = pixel & mask;
        int shift = mask_data.bytes * 8 - offset - 8;
        uint8_t c = shift > 0 ? (value >> shift) : (value << -shift);
        if (nbits < 8) {
            c |= (c >> nbits);
        }
        return c;
    }

    inline uint32_t read_pixel() const {
        uint32_t pixel = 0;
        for (int i = 0; i < 4; i++) {
            if (i < mask_data.bytes) {
                pixel <<= 8;
                pixel |= d()[n_read + i];
            }
        }
        return pixel;
    }

    GXColor read() override {
        uint32_t pixel = read_pixel();
        GXColor c;
        c.r = read_component(pixel, rmask, mask_data.rbits, mask_data.roff);
        c.g = read_component(pixel, gmask, mask_data.gbits, mask_data.goff);
        c.b = read_component(pixel, bmask, mask_data.bbits, mask_data.boff);
        if (mask_data.abits > 0) {
            c.a = read_component(pixel, amask, mask_data.abits, mask_data.aoff);
        } else {
            c.a = 255;
        }
        n_read += mask_data.bytes;
        return c;
    }

    const char *d() const { return static_cast<const char *>(m_data); }
    char *d() { return static_cast<char *>(m_data); }
    int n_read = 0;
    uint32_t rmask;
    uint32_t gmask;
    uint32_t bmask;
    uint32_t amask;
    MasksPerType mask_data;
    GLenum format;
};

/* This class handles reading of pixels from bitmap (1-bit depth) */
struct BitmapPixelStream: public PixelStreamBase {
    BitmapPixelStream() = default;

    inline uint8_t read_pixel() const {
        uint8_t byte = d()[n_read / 8];
        int shift = glparamstate.unpack_lsb_first ?
            (n_read % 8) : (7 - n_read % 8);
        bool bit = (byte >> shift) & 0x1;
        return bit ? 255 : 0;
    }

    GXColor read() override {
        uint8_t pixel = read_pixel();
        n_read++;
        return { pixel, pixel, pixel, 255 };
    }

    const uint8_t *d() const { return static_cast<const uint8_t *>(m_data); }
    uint8_t *d() { return static_cast<uint8_t *>(m_data); }
    int n_read = 0;
};

/* This is a generic class to read pixels whose components are expressed by 8,
 * 16, 32 bit wide integers or by 32 bit floats.
 */
template <typename T>
struct GenericPixelStream: public PixelStreamBase {
    GenericPixelStream(GLenum format, GLenum type):
        format(format),
        component_data(*find_component_data(format)) {}

    static const ComponentsPerFormat *find_component_data(GLenum format) {
        for (int i = 0; _ogx_pixels_components_per_format[i].format != 0; i++) {
            if (_ogx_pixels_components_per_format[i].format == format) {
                return &_ogx_pixels_components_per_format[i];
            }
        }
        return nullptr;
    }

    int pitch_for_width(int width) {
        return width * component_data.components_per_pixel * sizeof(T);
    }

    GXColor read() override {
        union {
            uint8_t components[4];
            GXColor c;
        } pixel = { 0, 0, 0, 255 };

        const ComponentsPerFormat &cd = component_data;
        for (int i = 0; i < cd.components_per_pixel; i++) {
            pixel.components[cd.component_index[i]] = component(d()[n_read++]);
        }

        /* Some formats require a special handling */
        if (cd.format == GL_INTENSITY ||
            cd.format == GL_LUMINANCE ||
            cd.format == GL_LUMINANCE_ALPHA) {
            pixel.c.g = pixel.c.b = pixel.c.r;
            if (cd.format == GL_INTENSITY) pixel.c.a = pixel.c.r;
        }

        return pixel.c;
    }

    T *d() { return static_cast<T *>(m_data); }
    const T *d() const { return static_cast<const T *>(m_data); }
    GLenum format;
    int n_read = 0;
    ComponentsPerFormat component_data;
};

#endif /* OPENGX_PIXEL_STREAM_H */
