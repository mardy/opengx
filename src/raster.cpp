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

#include "clip.h"
#include "debug.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>
#include <type_traits>

static void set_current_raster_pos(const guVector *pos)
{
    guVector pos_mv;
    guVecMultiply(glparamstate.modelview_matrix, pos, &pos_mv);

    if (_ogx_clip_is_point_clipped(&pos_mv)) {
        glparamstate.raster_pos_valid = false;
        return;
    }

    /* Apply the projection transformation */
    guVector pos_pj;
    mtx44project(glparamstate.projection_matrix, &pos_mv, &pos_pj);

    /* And the viewport transformation */
    float ox = glparamstate.viewport[2] / 2 + glparamstate.viewport[0];
    float oy = glparamstate.viewport[3] / 2 + glparamstate.viewport[1];
    glparamstate.raster_pos[0] =
        (glparamstate.viewport[2] * pos_pj.x) / 2 + ox;
    glparamstate.raster_pos[1] =
        (glparamstate.viewport[3] * pos_pj.y) / 2 + oy;
    const float n = glparamstate.depth_near;
    const float f = glparamstate.depth_far;
    glparamstate.raster_pos[2] = (pos_pj.z * (f - n) + (f + n)) / 2;
    glparamstate.raster_pos_valid = true;
}

static inline void set_pos(float x, float y, float z = 1.0)
{
    guVector p = { x, y, z };
    set_current_raster_pos(&p);
}

static inline void set_pos(float x, float y, float z, float w)
{
    set_pos(x / w, y / w, z / w);
}

void glRasterPos2d(GLdouble x, GLdouble y) { set_pos(x, y); }
void glRasterPos2f(GLfloat x, GLfloat y) { set_pos(x, y); }
void glRasterPos2i(GLint x, GLint y) { set_pos(x, y); }
void glRasterPos2s(GLshort x, GLshort y) { set_pos(x, y); }
void glRasterPos3d(GLdouble x, GLdouble y, GLdouble z) { set_pos(x, y, z); }
void glRasterPos3f(GLfloat x, GLfloat y, GLfloat z) { set_pos(x, y, z); }
void glRasterPos3i(GLint x, GLint y, GLint z) { set_pos(x, y, z); }
void glRasterPos3s(GLshort x, GLshort y, GLshort z) { set_pos(x, y, z); }
void glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { set_pos(x, y, z, w); }
void glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { set_pos(x, y, z, w); }
void glRasterPos4i(GLint x, GLint y, GLint z, GLint w) { set_pos(x, y, z, w); }
void glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w) { set_pos(x, y, z, w); }
void glRasterPos2dv(const GLdouble *v) { set_pos(v[0], v[1]); }
void glRasterPos2fv(const GLfloat *v) { set_pos(v[0], v[1]); }
void glRasterPos2iv(const GLint *v) { set_pos(v[0], v[1]); }
void glRasterPos2sv(const GLshort *v) { set_pos(v[0], v[1]); }
void glRasterPos3dv(const GLdouble *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3fv(const GLfloat *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3iv(const GLint *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3sv(const GLshort *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos4dv(const GLdouble *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4fv(const GLfloat *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4iv(const GLint *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4sv(const GLshort *v) { set_pos(v[0], v[1], v[2], v[3]); }

static void set_pixel_map(GLenum map, GLsizei mapsize, uint8_t *values)
{
    int index = map - GL_PIXEL_MAP_I_TO_I_SIZE;
    if (index < 0 || index >= 10) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    if (!glparamstate.pixel_maps) {
        glparamstate.pixel_maps =
            (OgxPixelMapTables*)malloc(sizeof(OgxPixelMapTables));
        memset(glparamstate.pixel_maps->sizes, 0,
               sizeof(glparamstate.pixel_maps->sizes));
    }

    glparamstate.pixel_maps->sizes[index] = mapsize;
    memcpy(glparamstate.pixel_maps->maps[index], values, mapsize);
}

void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] * 255;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] >> 24;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] >> 8;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

template <typename T>
void get_pixel_map(GLenum map, T *values)
{
    int index = map - GL_PIXEL_MAP_I_TO_I_SIZE;
    if (index < 0 || index >= 10) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    if (!glparamstate.pixel_maps) {
        *values = 0;
        return;
    }

    uint8_t map_size = glparamstate.pixel_maps->sizes[index];
    for (int i = 0; i < map_size; i++) {
        T value = glparamstate.pixel_maps->maps[index][i];
        /* We must map value to the target type: use full range for integer
         * types, and 0.0-1.0 for floats */
        if constexpr (std::is_floating_point<T>::value) {
            values[i] = value / 255.0f;
        } else {
            for (int b = 1; b < sizeof(T); b++) {
                value |= value << 8;
            }
            values[i] = value;
        }
    }
}

void glGetPixelMapfv(GLenum map, GLfloat *values)
{
    get_pixel_map(map, values);
}

void glGetPixelMapuiv(GLenum map, GLuint *values)
{
    get_pixel_map(map, values);
}

void glGetPixelMapusv(GLenum map, GLushort *values)
{
    get_pixel_map(map, values);
}
