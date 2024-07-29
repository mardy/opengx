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

#include "debug.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

void glVertex2d(GLdouble x, GLdouble y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2i(GLint x, GLint y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (glparamstate.imm_mode.current_numverts >= glparamstate.imm_mode.current_vertices_size) {
        if (!glparamstate.imm_mode.current_vertices) return;
        int current_size = glparamstate.imm_mode.current_vertices_size;
        int new_size = current_size < 256 ? (current_size * 2) : (current_size + 256);
        void *new_buffer = realloc(glparamstate.imm_mode.current_vertices,
                                   new_size * sizeof(VertexData));
        if (!new_buffer) {
            warning("Failed to reallocate memory for vertex buffer (%d)", errno);
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
        glparamstate.imm_mode.current_vertices_size = new_size;
        glparamstate.imm_mode.current_vertices = (VertexData*)new_buffer;
    }

    VertexData *vert = &glparamstate.imm_mode.current_vertices[glparamstate.imm_mode.current_numverts++];
    vert->tex[0] = glparamstate.imm_mode.current_texcoord[0];
    vert->tex[1] = glparamstate.imm_mode.current_texcoord[1];

    vert->color = gxcol_new_fv(glparamstate.imm_mode.current_color);

    floatcpy(vert->norm, glparamstate.imm_mode.current_normal, 3);

    vert->pos[0] = x;
    vert->pos[1] = y;
    vert->pos[2] = z;
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    glVertex3f(x / w, y / w, z / w);
}

void glVertex2fv(const GLfloat *v)
{
    glVertex2f(v[0], v[1]);
}

void glVertex3fv(const GLfloat *v)
{
    glVertex3f(v[0], v[1], v[2]);
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    glparamstate.imm_mode.current_normal[0] = nx;
    glparamstate.imm_mode.current_normal[1] = ny;
    glparamstate.imm_mode.current_normal[2] = nz;
}

void glNormal3fv(const GLfloat *v)
{
    glparamstate.imm_mode.current_normal[0] = v[0];
    glparamstate.imm_mode.current_normal[1] = v[1];
    glparamstate.imm_mode.current_normal[2] = v[2];
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = 1.0f;
}

void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    glColor3f(red / 255.0f, green / 255.0f, blue / 255.0f);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = clampf_01(alpha);
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = r / 255.0f;
    glparamstate.imm_mode.current_color[1] = g / 255.0f;
    glparamstate.imm_mode.current_color[2] = b / 255.0f;
    glparamstate.imm_mode.current_color[3] = a / 255.0f;
}

void glColor3fv(const GLfloat *v)
{
    glColor3f(v[0], v[1], v[2]);
}

void glColor4fv(const GLfloat *v)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(v[0]);
    glparamstate.imm_mode.current_color[1] = clampf_01(v[1]);
    glparamstate.imm_mode.current_color[2] = clampf_01(v[2]);
    glparamstate.imm_mode.current_color[3] = clampf_01(v[3]);
}

void glColor4ubv(const GLubyte *color)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = color[0] / 255.0f;
    glparamstate.imm_mode.current_color[1] = color[1] / 255.0f;
    glparamstate.imm_mode.current_color[2] = color[2] / 255.0f;
    glparamstate.imm_mode.current_color[3] = color[3] / 255.0f;
}

void glTexCoord2d(GLdouble u, GLdouble v)
{
    glTexCoord2f(u, v);
}

void glTexCoord2f(GLfloat u, GLfloat v)
{
    glparamstate.imm_mode.current_texcoord[0] = u;
    glparamstate.imm_mode.current_texcoord[1] = v;
}

void glTexCoord2i(GLint s, GLint t)
{
    glTexCoord2f(s, t);
}

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    glTexCoord2f(s, t);
    if (r != 0.0) {
        warning("glTexCoord3f not supported");
    }
}
