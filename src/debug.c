/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)
All rights reserved.

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

#include "debug.h"

#include <ogc/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

OgxLogMask _ogx_log_mask = 0;

static const struct {
    const char *feature;
    OgxLogMask mask;
} s_feature_masks[] = {
    { "warning", OGX_LOG_WARNING },
    { "call-lists", OGX_LOG_CALL_LISTS },
    { "lighting", OGX_LOG_LIGHTING },
    { "texture", OGX_LOG_TEXTURE },
    { "stencil", OGX_LOG_STENCIL },
    { NULL, 0 },
};

void _ogx_log_init()
{
    const char *log_env = getenv("OPENGX_DEBUG");
    if (log_env) {
        for (int i = 0; s_feature_masks[i].feature != NULL; i++) {
            if (strstr(log_env, s_feature_masks[i].feature) != NULL) {
                _ogx_log_mask |= s_feature_masks[i].mask;
            }
        }
        if (strcmp(log_env, "all") == 0) {
            _ogx_log_mask = 0xffffffff;
        }
    }
}

static void read_pixel(void *data, uint8_t format, int x, int y,
                       int16_t width, GXColor *color)
{
    uint8_t *texels = data;
    int tex_width = (width + 3) / 4 * 4;
    u32 offset = (((y >> 2) << 4) * tex_width) +
        ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);
    color->a = texels[offset];
    color->r = texels[offset + 1];
    color->g = texels[offset + 32];
    color->b = texels[offset + 33];
}

static void read_pixel_4bits(void *data, int x, int y,
                       int16_t width, uint8_t *value)
{
    uint8_t *texels = data;
    int blocks_per_row = (width + 7) / 8;
    int block_y = y / 8;
    int block_x = x / 8;
    u32 offset = block_y * blocks_per_row * 32 +
        block_x * 32 + (y % 8) * 4 + (x % 8) / 2;
    uint8_t v = (x % 2 == 0) ? (texels[offset] >> 4) : (texels[offset] & 0x0f);
    *value = v | (v << 4);
}

static void dump_mono_4bits(const char *filename,
                            int16_t width, int16_t height, void *data)
{
    FILE *file = fopen(filename, "w");
    if (!file) exit(0);
    fprintf(file, "P5 %d %d 255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t value;
            read_pixel_4bits(data, x, y, width, &value);
            fwrite(&value, 1, 1, file);
        }
    }
    fclose(file);
}

void _ogx_debug_dump_texture(const char *filename, uint8_t format,
                             int16_t width, int16_t height, void *data)
{
    char buffer[25];
    static int prog = 0;

    sprintf(buffer, "%03d-%s", ++prog, filename);
    if (format == GX_TF_I4 ||
        format == GX_CTF_R4) {
        dump_mono_4bits(buffer, width, height, data);
        return;
    }
    FILE *file = fopen(buffer, "w");
    if (!file) exit(0);
    fprintf(file, "P6 %d %d 255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            GXColor color;
            read_pixel(data, format, x, y, width, &color);
            fwrite(&color.r, 1, 1, file);
            fwrite(&color.g, 1, 1, file);
            fwrite(&color.b, 1, 1, file);
        }
    }
    fclose(file);
}
