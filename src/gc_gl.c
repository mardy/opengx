
/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
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

*****************************************************************************

             BASIC WII/GC OPENGL-LIKE IMPLEMENTATION

     This is a very basic OGL-like implementation. Don't expect any
     advanced (or maybe basic) features from the OGL spec.
     The support is very limited in some cases, you shoud read the
     README file which comes with the source to have an idea of the
     limits and how you can tune or modify this file to adapt the
     source to your neeeds.
     Take in mind this is not very fast. The code is intended to be
     tiny and much as portable as possible and easy to compile so
     there's lot of room for improvement.

*****************************************************************************/

#include "debug.h"
#include "image_DXT.h"
#include "opengx.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <gctypes.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ROUND_32B(x) (((x) + 31) & (~31))

glparams_ _ogx_state;

typedef struct
{
    uint8_t ambient_mask;
    uint8_t diffuse_mask;
    uint8_t specular_mask;
} LightMasks;

static const GLubyte gl_null_string[1] = { 0 };
char _ogx_log_level = 0;

static void swap_rgba(unsigned char *pixels, int num_pixels);
static void swap_rgb565(unsigned short *pixels, int num_pixels);
static void conv_rgb_to_rgb565(unsigned char *, void *, const unsigned int, const unsigned int);
static void conv_rgba_to_rgb565(unsigned char *, void *, const unsigned int, const unsigned int);
static void conv_rgba_to_luminance_alpha(unsigned char *src, void *dst, const unsigned int width, const unsigned int height);
static void scramble_2b(unsigned short *src, void *dst, const unsigned int width, const unsigned int height);
static void scramble_4b(unsigned char *src, void *dst, const unsigned int width, const unsigned int height);

static void scale_internal(int components, int widthin, int heightin,
                           const unsigned char *datain,
                           int widthout, int heightout, unsigned char *dataout);

static void draw_arrays_pos_normal_texc(float *ptr_pos, float *ptr_texc, float *ptr_normal,
                                        int count, bool loop);
static void draw_arrays_pos_normal(float *ptr_pos, float *ptr_normal, int count, bool loop);
static void draw_arrays_general(float *ptr_pos, float *ptr_normal, float *ptr_texc, float *ptr_color,
                                int count, int ne, int color_provide, int texen, bool loop);

#define MODELVIEW_UPDATE                                           \
    {                                                              \
        float trans[3][4];                                         \
        int i;                                                     \
        int j;                                                     \
        for (i = 0; i < 3; i++)                                    \
            for (j = 0; j < 4; j++)                                \
                trans[i][j] = glparamstate.modelview_matrix[j][i]; \
                                                                   \
        GX_LoadPosMtxImm(trans, GX_PNMTX3);                        \
        GX_SetCurrentMtx(GX_PNMTX3);                               \
    }

#define PROJECTION_UPDATE                                           \
    {                                                               \
        float trans[4][4];                                          \
        int i;                                                      \
        int j;                                                      \
        for (i = 0; i < 4; i++)                                     \
            for (j = 0; j < 4; j++)                                 \
                trans[i][j] = glparamstate.projection_matrix[j][i]; \
        if (trans[3][3] != 0)                                       \
            GX_LoadProjectionMtx(trans, GX_ORTHOGRAPHIC);           \
        else                                                        \
            GX_LoadProjectionMtx(trans, GX_PERSPECTIVE);            \
    }

#define NORMAL_UPDATE                                                  \
    {                                                                  \
        int i, j;                                                      \
        Mtx mvinverse, normalm, modelview;                             \
        for (i = 0; i < 3; i++)                                        \
            for (j = 0; j < 4; j++)                                    \
                modelview[i][j] = glparamstate.modelview_matrix[j][i]; \
                                                                       \
        guMtxInverse(modelview, mvinverse);                            \
        guMtxTranspose(mvinverse, normalm);                            \
        GX_LoadNrmMtxImm(normalm, GX_PNMTX3);                          \
    }

/* Deduce the projection type (perspective vs orthogonal) and the values of the
 * near and far clipping plane from the projection matrix.
 * Note that the formulas for computing "near" and "far" are only valid for
 * matrices created by opengx or by the gu* family of GX functions. OpenGL
 * books use different formulas.
 */
static void get_projection_info(u8 *type, float *near, float *far)
{
    float A, B;

    A = glparamstate.projection_matrix[2][2];
    B = glparamstate.projection_matrix[3][2];

    if (glparamstate.projection_matrix[3][3] == 0) {
        *type = GX_PERSPECTIVE;
        *near = B / (A - 1.0);
    } else {
        *type = GX_ORTHOGRAPHIC;
        *near = (B + 1.0) / A;
    }
    *far = B / A;
}

void ogx_initialize()
{
    const char *log_env = getenv("OPENGX_DEBUG");
    if (log_env) {
        _ogx_log_level = log_env[0] - '0';
    }

    GX_SetDispCopyGamma(GX_GM_1_0);
    int i;
    for (i = 0; i < _MAX_GL_TEX; i++) {
        texture_list[i].used = 0;
        texture_list[i].data = 0;
    }

    glparamstate.blendenabled = 0;
    glparamstate.srcblend = GX_BL_ONE;
    glparamstate.dstblend = GX_BL_ZERO;

    glparamstate.clear_color.r = 0; // Black as default
    glparamstate.clear_color.g = 0;
    glparamstate.clear_color.b = 0;
    glparamstate.clear_color.a = 1;
    glparamstate.clearz = 1.0f;

    glparamstate.ztest = GX_FALSE; // Depth test disabled but z write enabled
    glparamstate.zfunc = GX_LESS;  // Although write is efectively disabled
    glparamstate.zwrite = GX_TRUE; // unless test is enabled

    glparamstate.matrixmode = 1; // Modelview default mode
    glparamstate.glcurtex = 0;   // Default texture is 0 (nonstardard)
    GX_SetNumChans(1);           // One modulation color (as glColor)
    glDisable(GL_TEXTURE_2D);

    glparamstate.glcullmode = GL_BACK;
    glparamstate.cullenabled = 0;
    glparamstate.frontcw = 0; // By default front is CCW
    glDisable(GL_CULL_FACE);
    glparamstate.texture_env_mode = GL_MODULATE;

    glparamstate.cur_proj_mat = -1;
    glparamstate.cur_modv_mat = -1;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glparamstate.imm_mode.current_color[0] = 1.0f; // Default imm data, could be wrong
    glparamstate.imm_mode.current_color[1] = 1.0f;
    glparamstate.imm_mode.current_color[2] = 1.0f;
    glparamstate.imm_mode.current_color[3] = 1.0f;
    glparamstate.imm_mode.current_texcoord[0] = 0;
    glparamstate.imm_mode.current_texcoord[1] = 0;
    glparamstate.imm_mode.current_normal[0] = 0;
    glparamstate.imm_mode.current_normal[1] = 0;
    glparamstate.imm_mode.current_normal[2] = 1.0f;
    glparamstate.imm_mode.current_numverts = 0;

    glparamstate.vertex_enabled = 0; // DisableClientState on everything
    glparamstate.normal_enabled = 0;
    glparamstate.texcoord_enabled = 0;
    glparamstate.index_enabled = 0;
    glparamstate.color_enabled = 0;

    glparamstate.texture_enabled = 0;

    // Set up lights default states
    glparamstate.lighting.enabled = 0;
    for (i = 0; i < MAX_LIGHTS; i++) {
        glparamstate.lighting.lights[i].enabled = false;

        glparamstate.lighting.lights[i].atten[0] = 1;
        glparamstate.lighting.lights[i].atten[1] = 0;
        glparamstate.lighting.lights[i].atten[2] = 0;

        /* The default value for light position is (0, 0, 1), but since it's a
         * directional light we need to transform it to 100000. */
        glparamstate.lighting.lights[i].position[0] = 0;
        glparamstate.lighting.lights[i].position[1] = 0;
        glparamstate.lighting.lights[i].position[2] = 100000;
        glparamstate.lighting.lights[i].position[3] = 0;

        glparamstate.lighting.lights[i].direction[0] = 0;
        glparamstate.lighting.lights[i].direction[1] = 0;
        glparamstate.lighting.lights[i].direction[2] = -1;

        glparamstate.lighting.lights[i].spot_direction[0] = 0;
        glparamstate.lighting.lights[i].spot_direction[1] = 0;
        glparamstate.lighting.lights[i].spot_direction[2] = -1;

        glparamstate.lighting.lights[i].ambient_color[0] = 0;
        glparamstate.lighting.lights[i].ambient_color[1] = 0;
        glparamstate.lighting.lights[i].ambient_color[2] = 0;
        glparamstate.lighting.lights[i].ambient_color[3] = 1;

        if (i == 0) {
            glparamstate.lighting.lights[i].diffuse_color[0] = 1;
            glparamstate.lighting.lights[i].diffuse_color[1] = 1;
            glparamstate.lighting.lights[i].diffuse_color[2] = 1;

            glparamstate.lighting.lights[i].specular_color[0] = 1;
            glparamstate.lighting.lights[i].specular_color[1] = 1;
            glparamstate.lighting.lights[i].specular_color[2] = 1;
        } else {
            glparamstate.lighting.lights[i].diffuse_color[0] = 0;
            glparamstate.lighting.lights[i].diffuse_color[1] = 0;
            glparamstate.lighting.lights[i].diffuse_color[2] = 0;

            glparamstate.lighting.lights[i].specular_color[0] = 0;
            glparamstate.lighting.lights[i].specular_color[1] = 0;
            glparamstate.lighting.lights[i].specular_color[2] = 0;
        }
        glparamstate.lighting.lights[i].diffuse_color[3] = 1;
        glparamstate.lighting.lights[i].specular_color[3] = 1;

        glparamstate.lighting.lights[i].spot_cutoff = 180.0f;
        glparamstate.lighting.lights[i].spot_exponent = 0;
    }

    glparamstate.lighting.globalambient[0] = 0.2f;
    glparamstate.lighting.globalambient[1] = 0.2f;
    glparamstate.lighting.globalambient[2] = 0.2f;
    glparamstate.lighting.globalambient[3] = 1.0f;

    glparamstate.lighting.matambient[0] = 0.2f;
    glparamstate.lighting.matambient[1] = 0.2f;
    glparamstate.lighting.matambient[2] = 0.2f;
    glparamstate.lighting.matambient[3] = 1.0f;

    glparamstate.lighting.matdiffuse[0] = 0.8f;
    glparamstate.lighting.matdiffuse[1] = 0.8f;
    glparamstate.lighting.matdiffuse[2] = 0.8f;
    glparamstate.lighting.matdiffuse[3] = 1.0f;

    glparamstate.lighting.matemission[0] = 0.0f;
    glparamstate.lighting.matemission[1] = 0.0f;
    glparamstate.lighting.matemission[2] = 0.0f;
    glparamstate.lighting.matemission[3] = 1.0f;

    glparamstate.lighting.matspecular[0] = 0.0f;
    glparamstate.lighting.matspecular[1] = 0.0f;
    glparamstate.lighting.matspecular[2] = 0.0f;
    glparamstate.lighting.matspecular[3] = 1.0f;
    glparamstate.lighting.matshininess = 0.0f;

    glparamstate.lighting.color_material_enabled = 0;
    glparamstate.lighting.color_material_mode = GL_AMBIENT_AND_DIFFUSE;

    glparamstate.fog.enabled = false;
    glparamstate.fog.mode = GL_EXP;
    glparamstate.fog.color[0] = 0.0f;
    glparamstate.fog.color[1] = 0.0f;
    glparamstate.fog.color[2] = 0.0f;
    glparamstate.fog.color[3] = 0.0f;
    glparamstate.fog.density = 1.0f;
    glparamstate.fog.start = 0.0f;
    glparamstate.fog.end = 1.0f;

    glparamstate.error = GL_NO_ERROR;

    // Setup data types for every possible attribute

    // Typical straight float
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    // Mark all the hardware data as dirty, so it will be recalculated
    // and uploaded again to the hardware
    glparamstate.dirty.all = ~0;
}

void glEnable(GLenum cap)
{ // TODO
    switch (cap) {
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled = 1;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 1;
        break;
    case GL_CULL_FACE:
        switch (glparamstate.glcullmode) {
        case GL_FRONT:
            if (glparamstate.frontcw)
                GX_SetCullMode(GX_CULL_FRONT);
            else
                GX_SetCullMode(GX_CULL_BACK);
            break;
        case GL_BACK:
            if (glparamstate.frontcw)
                GX_SetCullMode(GX_CULL_BACK);
            else
                GX_SetCullMode(GX_CULL_FRONT);
            break;
        case GL_FRONT_AND_BACK:
            GX_SetCullMode(GX_CULL_ALL);
            break;
        };
        glparamstate.cullenabled = 1;
        break;
    case GL_BLEND:
        glparamstate.blendenabled = 1;
        glparamstate.dirty.bits.dirty_blend = 1;
        break;
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_TRUE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_FOG:
        glparamstate.fog.enabled = 1;
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 1;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 1;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    default:
        break;
    }
}

void glDisable(GLenum cap)
{ // TODO
    switch (cap) {
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled = 0;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 0;
        break;
    case GL_CULL_FACE:
        GX_SetCullMode(GX_CULL_NONE);
        glparamstate.cullenabled = 0;
        break;
    case GL_BLEND:
        glparamstate.blendenabled = 0;
        glparamstate.dirty.bits.dirty_blend = 1;
        break;
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_FALSE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 0;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 0;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    default:
        break;
    }
}

void glFogf(GLenum pname, GLfloat param)
{
    switch (pname) {
    case GL_FOG_MODE:
        glFogi(pname, (int)param);
        break;
    case GL_FOG_DENSITY:
        glparamstate.fog.density = param;
        break;
    case GL_FOG_START:
        glparamstate.fog.start = param;
        break;
    case GL_FOG_END:
        glparamstate.fog.end = param;
        break;
    }
}

void glFogi(GLenum pname, GLint param)
{
    switch (pname) {
    case GL_FOG_MODE:
        glparamstate.fog.mode = param;
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        glFogf(pname, param);
        break;
    }
}

void glFogfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        glFogf(pname, params[0]);
        break;
    case GL_FOG_COLOR:
        floatcpy(glparamstate.fog.color, params, 4);
        break;
    }
}

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    int lnum = light - GL_LIGHT0;

    switch (pname) {
    case GL_CONSTANT_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[0] = param;
        break;
    case GL_LINEAR_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[1] = param;
        break;
    case GL_QUADRATIC_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[2] = param;
        break;
    case GL_SPOT_CUTOFF:
        glparamstate.lighting.lights[lnum].spot_cutoff = param;
        break;
    case GL_SPOT_EXPONENT:
        glparamstate.lighting.lights[lnum].spot_exponent = (int)param;
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_lighting = 1;
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    int lnum = light - GL_LIGHT0;
    switch (pname) {
    case GL_SPOT_DIRECTION:
        floatcpy(glparamstate.lighting.lights[lnum].spot_direction, params, 3);
        break;
    case GL_POSITION:
        if (params[3] == 0) {
            // Push the light far away, calculate the direction and normalize it
            glparamstate.lighting.lights[lnum].position[0] = params[0] * 100000;
            glparamstate.lighting.lights[lnum].position[1] = params[1] * 100000;
            glparamstate.lighting.lights[lnum].position[2] = params[2] * 100000;
        } else {
            glparamstate.lighting.lights[lnum].position[0] = params[0];
            glparamstate.lighting.lights[lnum].position[1] = params[1];
            glparamstate.lighting.lights[lnum].position[2] = params[2];
        }
        glparamstate.lighting.lights[lnum].position[3] = params[3];
        {
            float modv[3][4];
            int i;
            int j;
            for (i = 0; i < 3; i++)
                for (j = 0; j < 4; j++)
                    modv[i][j] = glparamstate.modelview_matrix[j][i];
            guVecMultiply(modv, (guVector *)glparamstate.lighting.lights[lnum].position, (guVector *)glparamstate.lighting.lights[lnum].position);
        }
        break;
    case GL_DIFFUSE:
        floatcpy(glparamstate.lighting.lights[lnum].diffuse_color, params, 4);
        break;
    case GL_AMBIENT:
        floatcpy(glparamstate.lighting.lights[lnum].ambient_color, params, 4);
        break;
    case GL_SPECULAR:
        floatcpy(glparamstate.lighting.lights[lnum].specular_color, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_lighting = 1;
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        floatcpy(glparamstate.lighting.globalambient, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_material = 1;
};

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_DIFFUSE:
        floatcpy(glparamstate.lighting.matdiffuse, params, 4);
        break;
    case GL_AMBIENT:
        floatcpy(glparamstate.lighting.matambient, params, 4);
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        floatcpy(glparamstate.lighting.matambient, params, 4);
        floatcpy(glparamstate.lighting.matdiffuse, params, 4);
        break;
    case GL_EMISSION:
        floatcpy(glparamstate.lighting.matemission, params, 4);
        break;
    case GL_SPECULAR:
        floatcpy(glparamstate.lighting.matspecular, params, 4);
        break;
    case GL_SHININESS:
        glparamstate.lighting.matshininess = params[0];
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_material = 1;
};

void glColorMaterial(GLenum face, GLenum mode)
{
    /* TODO: support the face parameter */
    glparamstate.lighting.color_material_mode = mode;
}

void glCullFace(GLenum mode)
{
    glparamstate.glcullmode = mode;
    if (glparamstate.cullenabled)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (texture < 0 || texture >= _MAX_GL_TEX)
        return;

    // If the texture has been initialized (data!=0) then load it to GX reg 0
    if (texture_list[texture].used) {
        glparamstate.glcurtex = texture;

        if (texture_list[texture].data != 0)
            GX_LoadTexObj(&texture_list[glparamstate.glcurtex].texobj, GX_TEXMAP0);
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    const GLuint *texlist = textures;
    GX_DrawDone();
    while (n-- > 0) {
        int i = *texlist++;
        if (!(i < 0 || i >= _MAX_GL_TEX)) {
            if (texture_list[i].data != 0)
                free(texture_list[i].data);
            texture_list[i].data = 0;
            texture_list[i].used = 0;
        }
    }
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint *texlist = textures;
    int i;
    for (i = 0; i < _MAX_GL_TEX && n > 0; i++) {
        if (texture_list[i].used == 0) {
            texture_list[i].used = 1;
            texture_list[i].data = 0;
            texture_list[i].w = 0;
            texture_list[i].h = 0;
            texture_list[i].wraps = GX_REPEAT;
            texture_list[i].wrapt = GX_REPEAT;
            texture_list[i].bytespp = 0;
            texture_list[i].maxlevel = -1;
            texture_list[i].minlevel = 20;
            *texlist++ = i;
            n--;
        }
    }
}
void glBegin(GLenum mode)
{
    // Just discard all the data!
    glparamstate.imm_mode.current_numverts = 0;
    glparamstate.imm_mode.prim_type = mode;
    if (!glparamstate.imm_mode.current_vertices) {
        int count = 64;
        void *buffer = malloc(count * sizeof(VertexData));
        if (buffer) {
            glparamstate.imm_mode.current_vertices = buffer;
            glparamstate.imm_mode.current_vertices_size = count;
        } else {
            set_error(GL_OUT_OF_MEMORY);
        }
    }
}

void glEnd()
{
    glInterleavedArrays(GL_T2F_C4F_N3F_V3F, 0, glparamstate.imm_mode.current_vertices);
    glDrawArrays(glparamstate.imm_mode.prim_type, 0, glparamstate.imm_mode.current_numverts);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GX_SetViewport(x, y, width, height, 0.0f, 1.0f);
    GX_SetScissor(x, y, width, height);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GX_SetScissor(x, y, width, height);
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    glparamstate.imm_mode.current_color[0] = r / 255.0f;
    glparamstate.imm_mode.current_color[1] = g / 255.0f;
    glparamstate.imm_mode.current_color[2] = b / 255.0f;
    glparamstate.imm_mode.current_color[3] = a / 255.0f;
}
void glColor4ubv(const GLubyte *color)
{
    glparamstate.imm_mode.current_color[0] = color[0] / 255.0f;
    glparamstate.imm_mode.current_color[1] = color[1] / 255.0f;
    glparamstate.imm_mode.current_color[2] = color[2] / 255.0f;
    glparamstate.imm_mode.current_color[3] = color[3] / 255.0f;
}
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = clampf_01(alpha);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = 1.0f;
}

void glColor4fv(const GLfloat *v)
{
    glparamstate.imm_mode.current_color[0] = clampf_01(v[0]);
    glparamstate.imm_mode.current_color[1] = clampf_01(v[1]);
    glparamstate.imm_mode.current_color[2] = clampf_01(v[2]);
    glparamstate.imm_mode.current_color[3] = clampf_01(v[3]);
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

void glVertex2d(GLdouble x, GLdouble y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2i(GLint x, GLint y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2f(GLfloat x, GLfloat y)
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
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
        glparamstate.imm_mode.current_vertices_size = new_size;
        glparamstate.imm_mode.current_vertices = new_buffer;
    }

    // GL_T2F_C4F_N3F_V3F
    float *vert = glparamstate.imm_mode.current_vertices[glparamstate.imm_mode.current_numverts++];
    vert[0] = glparamstate.imm_mode.current_texcoord[0];
    vert[1] = glparamstate.imm_mode.current_texcoord[1];

    floatcpy(vert + 2, glparamstate.imm_mode.current_color, 4);

    floatcpy(vert + 6, glparamstate.imm_mode.current_normal, 3);

    vert[9] = x;
    vert[10] = y;
    vert[11] = z;
}

void glMatrixMode(GLenum mode)
{
    switch (mode) {
    case GL_MODELVIEW:
        glparamstate.matrixmode = 1;
        break;
    case GL_PROJECTION:
        glparamstate.matrixmode = 0;
        break;
    default:
        glparamstate.matrixmode = -1;
        break;
    }
}
void glPopMatrix(void)
{
    switch (glparamstate.matrixmode) {
    case 0:
        memcpy(glparamstate.projection_matrix, glparamstate.projection_stack[glparamstate.cur_proj_mat], sizeof(Mtx44));
        glparamstate.cur_proj_mat--;
    case 1:
        memcpy(glparamstate.modelview_matrix, glparamstate.modelview_stack[glparamstate.cur_modv_mat], sizeof(Mtx44));
        glparamstate.cur_modv_mat--;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glPushMatrix(void)
{
    switch (glparamstate.matrixmode) {
    case 0:
        glparamstate.cur_proj_mat++;
        memcpy(glparamstate.projection_stack[glparamstate.cur_proj_mat], glparamstate.projection_matrix, sizeof(Mtx44));
        break;
    case 1:
        glparamstate.cur_modv_mat++;
        memcpy(glparamstate.modelview_stack[glparamstate.cur_modv_mat], glparamstate.modelview_matrix, sizeof(Mtx44));
        break;
    default:
        break;
    }
}
void glLoadMatrixf(const GLfloat *m)
{
    switch (glparamstate.matrixmode) {
    case 0:
        memcpy(glparamstate.projection_matrix, m, sizeof(Mtx44));
        break;
    case 1:
        memcpy(glparamstate.modelview_matrix, m, sizeof(Mtx44));
        break;
    default:
        return;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glMultMatrixf(const GLfloat *m)
{
    Mtx44 curr;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)m);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)m);
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glLoadIdentity()
{
    float *mtrx;
    switch (glparamstate.matrixmode) {
    case 0:
        mtrx = &glparamstate.projection_matrix[0][0];
        break;
    case 1:
        mtrx = &glparamstate.modelview_matrix[0][0];
        break;
    default:
        return;
    }

    mtrx[0] = 1.0f;
    mtrx[1] = 0.0f;
    mtrx[2] = 0.0f;
    mtrx[3] = 0.0f;
    mtrx[4] = 0.0f;
    mtrx[5] = 1.0f;
    mtrx[6] = 0.0f;
    mtrx[7] = 0.0f;
    mtrx[8] = 0.0f;
    mtrx[9] = 0.0f;
    mtrx[10] = 1.0f;
    mtrx[11] = 0.0f;
    mtrx[12] = 0.0f;
    mtrx[13] = 0.0f;
    mtrx[14] = 0.0f;
    mtrx[15] = 1.0f;

    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    Mtx44 newmat;
    Mtx44 curr;
    newmat[0][0] = x;
    newmat[0][1] = 0.0f;
    newmat[0][2] = 0.0f;
    newmat[0][3] = 0.0f;
    newmat[1][0] = 0.0f;
    newmat[1][1] = y;
    newmat[1][2] = 0.0f;
    newmat[1][3] = 0.0f;
    newmat[2][0] = 0.0f;
    newmat[2][1] = 0.0f;
    newmat[2][2] = z;
    newmat[2][3] = 0.0f;
    newmat[3][0] = 0.0f;
    newmat[3][1] = 0.0f;
    newmat[3][2] = 0.0f;
    newmat[3][3] = 1.0f;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    Mtx44 newmat;
    Mtx44 curr;
    newmat[0][0] = 1.0f;
    newmat[0][1] = 0.0f;
    newmat[0][2] = 0.0f;
    newmat[0][3] = 0.0f;
    newmat[1][0] = 0.0f;
    newmat[1][1] = 1.0f;
    newmat[1][2] = 0.0f;
    newmat[1][3] = 0.0f;
    newmat[2][0] = 0.0f;
    newmat[2][1] = 0.0f;
    newmat[2][2] = 1.0f;
    newmat[2][3] = 0.0f;
    newmat[3][0] = x;
    newmat[3][1] = y;
    newmat[3][2] = z;
    newmat[3][3] = 1.0f;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    angle *= (M_PI / 180.0f);
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    Mtx44 newmat;
    Mtx44 curr;

    float imod = 1.0f / sqrtf(x * x + y * y + z * z);
    x *= imod;
    y *= imod;
    z *= imod;

    newmat[0][0] = t * x * x + c;
    newmat[0][1] = t * x * y + s * z;
    newmat[0][2] = t * x * z - s * y;
    newmat[0][3] = 0;
    newmat[1][0] = t * x * y - s * z;
    newmat[1][1] = t * y * y + c;
    newmat[1][2] = t * y * z + s * x;
    newmat[1][3] = 0;
    newmat[2][0] = t * x * z + s * y;
    newmat[2][1] = t * y * z - s * x;
    newmat[2][2] = t * z * z + c;
    newmat[2][3] = 0;
    newmat[3][0] = 0;
    newmat[3][1] = 0;
    newmat[3][2] = 0;
    newmat[3][3] = 1;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    glparamstate.clear_color.r = clampf_01(red) * 255.0f;
    glparamstate.clear_color.g = clampf_01(green) * 255.0f;
    glparamstate.clear_color.b = clampf_01(blue) * 255.0f;
    glparamstate.clear_color.a = clampf_01(alpha) * 255.0f;
}
void glClearDepth(GLclampd depth)
{
    glparamstate.clearz = clampf_01(depth);
}

// Clearing is simulated by rendering a big square with the depth value
// and the desired color
void glClear(GLbitfield mask)
{
    // Tweak the Z value to avoid floating point errors. dpeth goes from 0.001 to 0.998
    float depth = (0.998f * glparamstate.clearz) + 0.001f;
    if (mask & GL_DEPTH_BUFFER_BIT)
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    else
        GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    if (mask & GL_COLOR_BUFFER_BIT)
        GX_SetColorUpdate(GX_TRUE);
    else
        GX_SetColorUpdate(GX_TRUE);

    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GX_SetCullMode(GX_CULL_NONE);

    static float modl[3][4];
    modl[0][0] = 1.0f;
    modl[0][1] = 0.0f;
    modl[0][2] = 0.0f;
    modl[0][3] = 0.0f;
    modl[1][0] = 0.0f;
    modl[1][1] = 1.0f;
    modl[1][2] = 0.0f;
    modl[1][3] = 0.0f;
    modl[2][0] = 0.0f;
    modl[2][1] = 0.0f;
    modl[2][2] = 1.0f;
    modl[2][3] = 0.0f;
    GX_LoadPosMtxImm(modl, GX_PNMTX3);
    GX_SetCurrentMtx(GX_PNMTX3);

    Mtx44 proj;
    guOrtho(proj, -1, 1, -1, 1, -1, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    GX_SetNumChans(1);
    GX_SetNumTevStages(1);
    GX_SetNumTexGens(0);

    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_InvVtxCache();

    if (glparamstate.fog.enabled) {
        /* Disable fog while clearing */
        GX_SetFog(GX_FOG_NONE, 0.0, 0.0, 0.0, 0.0, glparamstate.clear_color);
    }

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(-1, -1, -depth);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_Position3f32(1, -1, -depth);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_Position3f32(1, 1, -depth);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_Position3f32(-1, 1, -depth);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_End();

    glparamstate.dirty.all = ~0;
}

void glDepthFunc(GLenum func)
{
    switch (func) {
    case GL_NEVER:
        glparamstate.zfunc = GX_NEVER;
        break;
    case GL_LESS:
        glparamstate.zfunc = GX_LESS;
        break;
    case GL_EQUAL:
        glparamstate.zfunc = GX_EQUAL;
        break;
    case GL_LEQUAL:
        glparamstate.zfunc = GX_LEQUAL;
        break;
    case GL_GREATER:
        glparamstate.zfunc = GX_GREATER;
        break;
    case GL_NOTEQUAL:
        glparamstate.zfunc = GX_NEQUAL;
        break;
    case GL_GEQUAL:
        glparamstate.zfunc = GX_GEQUAL;
        break;
    case GL_ALWAYS:
        glparamstate.zfunc = GX_ALWAYS;
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_z = 1;
}
void glDepthMask(GLboolean flag)
{
    if (flag == GL_FALSE || flag == 0)
        glparamstate.zwrite = GX_FALSE;
    else
        glparamstate.zwrite = GX_TRUE;
    glparamstate.dirty.bits.dirty_z = 1;
}

GLenum glGetError(void)
{
    GLenum error = glparamstate.error;
    glparamstate.error = GL_NO_ERROR;
    return error;
}

void glFlush() {} // All commands are sent immediately to draw, no queue, so pointless

// Waits for all the commands to be successfully executed
void glFinish()
{
    GX_DrawDone(); // Be careful, WaitDrawDone waits for the DD command, this sends AND waits for it
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    switch (sfactor) {
    case GL_ZERO:
        glparamstate.srcblend = GX_BL_ZERO;
        break;
    case GL_ONE:
        glparamstate.srcblend = GX_BL_ONE;
        break;
    case GL_SRC_COLOR:
        glparamstate.srcblend = GX_BL_SRCCLR;
        break;
    case GL_ONE_MINUS_SRC_COLOR:
        glparamstate.srcblend = GX_BL_INVSRCCLR;
        break;
    case GL_DST_COLOR:
        glparamstate.srcblend = GX_BL_DSTCLR;
        break;
    case GL_ONE_MINUS_DST_COLOR:
        glparamstate.srcblend = GX_BL_INVDSTCLR;
        break;
    case GL_SRC_ALPHA:
        glparamstate.srcblend = GX_BL_SRCALPHA;
        break;
    case GL_ONE_MINUS_SRC_ALPHA:
        glparamstate.srcblend = GX_BL_INVSRCALPHA;
        break;
    case GL_DST_ALPHA:
        glparamstate.srcblend = GX_BL_DSTALPHA;
        break;
    case GL_ONE_MINUS_DST_ALPHA:
        glparamstate.srcblend = GX_BL_INVDSTALPHA;
        break;
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        break; // Not supported
    }

    switch (dfactor) {
    case GL_ZERO:
        glparamstate.dstblend = GX_BL_ZERO;
        break;
    case GL_ONE:
        glparamstate.dstblend = GX_BL_ONE;
        break;
    case GL_SRC_COLOR:
        glparamstate.dstblend = GX_BL_SRCCLR;
        break;
    case GL_ONE_MINUS_SRC_COLOR:
        glparamstate.dstblend = GX_BL_INVSRCCLR;
        break;
    case GL_DST_COLOR:
        glparamstate.dstblend = GX_BL_DSTCLR;
        break;
    case GL_ONE_MINUS_DST_COLOR:
        glparamstate.dstblend = GX_BL_INVDSTCLR;
        break;
    case GL_SRC_ALPHA:
        glparamstate.dstblend = GX_BL_SRCALPHA;
        break;
    case GL_ONE_MINUS_SRC_ALPHA:
        glparamstate.dstblend = GX_BL_INVSRCALPHA;
        break;
    case GL_DST_ALPHA:
        glparamstate.dstblend = GX_BL_DSTALPHA;
        break;
    case GL_ONE_MINUS_DST_ALPHA:
        glparamstate.dstblend = GX_BL_INVDSTALPHA;
        break;
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        break; // Not supported
    }

    glparamstate.dirty.bits.dirty_blend = 1;
}

void glLineWidth(GLfloat width)
{
    GX_SetLineWidth((unsigned int)(width * 16), GX_TO_ZERO);
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    /* For the time being, all the parameters we support take integer values */
    glTexEnvi(target, pname, param);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        glparamstate.texture_env_mode = param;
        break;
    }
}

// If bytes per pixel is decimal (0,5 0,25 ...) we encode the number
// as the divisor in a negative way
static int calc_memory(int w, int h, int bytespp)
{
    if (bytespp > 0) {
        return w * h * bytespp;
    } else {
        return w * h / (-bytespp);
    }
}
// Returns the number of bytes required to store a texture with all its bitmaps
static int calc_tex_size(int w, int h, int bytespp)
{
    int size = 0;
    while (w > 1 || h > 1) {
        int mipsize = calc_memory(w, h, bytespp);
        if ((mipsize % 32) != 0)
            mipsize += (32 - (mipsize % 32)); // Alignment
        size += mipsize;
        if (w != 1)
            w = w / 2;
        if (h != 1)
            h = h / 2;
    }
    return size;
}
// Returns the number of bytes required to store a texture with all its bitmaps
static int calc_original_size(int level, int s)
{
    while (level > 0) {
        s = 2 * s;
        level--;
    }
    return s;
}
// Given w,h,level,and bpp, returns the offset to the mipmap at level "level"
static int calc_mipmap_offset(int level, int w, int h, int b)
{
    int size = 0;
    while (level > 0) {
        int mipsize = calc_memory(w, h, b);
        if ((mipsize % 32) != 0)
            mipsize += (32 - (mipsize % 32)); // Alignment
        size += mipsize;
        if (w != 1)
            w = w / 2;
        if (h != 1)
            h = h / 2;
        level--;
    }
    return size;
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *data)
{

    // Initial checks
    if (texture_list[glparamstate.glcurtex].used == 0)
        return;
    if (target != GL_TEXTURE_2D)
        return; // FIXME Implement non 2D textures

    GX_DrawDone(); // Very ugly, we should have a list of used textures and only wait if we are using the curr tex.
                   // This way we are sure that we are not modifying a texture which is being drawn

    gltexture_ *currtex = &texture_list[glparamstate.glcurtex];

    // Just simplify it a little ;)
    if (internalFormat == GL_BGR)
        internalFormat = GL_RGB;
    else if (internalFormat == GL_BGRA)
        internalFormat = GL_RGBA;
    else if (internalFormat == GL_RGB4)
        internalFormat = GL_RGB;
    else if (internalFormat == GL_RGB5)
        internalFormat = GL_RGB;
    else if (internalFormat == GL_RGB8)
        internalFormat = GL_RGB;
    else if (internalFormat == 3)
        internalFormat = GL_RGB;
    else if (internalFormat == 4)
        internalFormat = GL_RGBA;

    // Simplify but keep in mind the swapping
    int needswap = 0;
    if (format == GL_BGR) {
        format = GL_RGB;
        needswap = 1;
    }
    if (format == GL_BGRA) {
        format = GL_RGBA;
        needswap = 1;
    }

    // Fallbacks for formats which we can't handle
    if (internalFormat == GL_COMPRESSED_RGBA_ARB)
        internalFormat = GL_RGBA; // Cannot compress RGBA!

    // Simplify and avoid stupid conversions (which waste space for no gain)
    if (format == GL_RGB && internalFormat == GL_RGBA)
        internalFormat = GL_RGB;

    if (format == GL_LUMINANCE_ALPHA && internalFormat == GL_RGBA)
        internalFormat = GL_LUMINANCE_ALPHA;

    // TODO: Implement GL_LUMINANCE/GL_INTENSITY? and fallback from GL_LUM_ALPHA to GL_LUM instead of RGB (2bytes to 1byte)
    //	if (format == GL_LUMINANCE_ALPHA && internalFormat == GL_RGB) internalFormat = GL_LUMINANCE_ALPHA;

    int bytesperpixelinternal = 4, bytesperpixelsrc = 4;

    if (format == GL_RGB)
        bytesperpixelsrc = 3;
    else if (format == GL_RGBA)
        bytesperpixelsrc = 4;
    else if (format == GL_LUMINANCE_ALPHA)
        bytesperpixelsrc = 2;

    if (internalFormat == GL_RGB)
        bytesperpixelinternal = 2;
    else if (internalFormat == GL_RGBA)
        bytesperpixelinternal = 4;
    else if (internalFormat == GL_LUMINANCE_ALPHA)
        bytesperpixelinternal = 2;

    if (internalFormat == GL_COMPRESSED_RGB_ARB && format == GL_RGB) // Only compress on demand and non-alpha textures
        bytesperpixelinternal = -2;                                  // 0.5 bytes per pixel

    if (bytesperpixelinternal < 0 && (width < 8 || height < 8))
        return; // Cannot take compressed textures under 8x8 (4 blocks of 4x4, 32B)

    // We *may* need to delete and create a new texture, depending if the user wants to add some mipmap levels
    // or wants to create a new texture from scratch
    int wi = calc_original_size(level, width);
    int he = calc_original_size(level, height);
    currtex->bytespp = bytesperpixelinternal;

    // Check if the texture has changed its geometry and proceed to delete it
    // If the specified level is zero, create a onelevel texture to save memory
    if (wi != currtex->w || he != currtex->h || bytesperpixelinternal != currtex->bytespp) {
        if (currtex->data != 0)
            free(currtex->data);
        if (level == 0) {
            int required_size = calc_memory(width, height, bytesperpixelinternal);
            int tex_size_rnd = ROUND_32B(required_size);
            currtex->data = memalign(32, tex_size_rnd);
            currtex->onelevel = 1;
        } else {
            int required_size = calc_tex_size(wi, he, bytesperpixelinternal);
            int tex_size_rnd = ROUND_32B(required_size);
            currtex->data = memalign(32, tex_size_rnd);
            currtex->onelevel = 0;
        }
        currtex->minlevel = level;
        currtex->maxlevel = level;
    }
    currtex->w = wi;
    currtex->h = he;
    if (currtex->maxlevel < level)
        currtex->maxlevel = level;
    if (currtex->minlevel > level)
        currtex->minlevel = level;

    if (currtex->onelevel == 1 && level != 0) {
        // We allocated a onelevel texture (base level 0) but now
        // we are uploading a non-zero level, so we need to create a mipmap capable buffer
        // and copy the level zero texture
        unsigned int tsize = calc_memory(wi, he, bytesperpixelinternal);
        unsigned char *tempbuf = malloc(tsize);
        memcpy(tempbuf, currtex->data, tsize);
        free(currtex->data);

        int required_size = calc_tex_size(wi, he, bytesperpixelinternal);
        int tex_size_rnd = ROUND_32B(required_size);
        currtex->data = memalign(32, tex_size_rnd);
        currtex->onelevel = 0;

        memcpy(currtex->data, tempbuf, tsize);
        free(tempbuf);
    }

    // Inconditionally convert to 565 all inputs without alpha channel
    // Alpha inputs may be stripped if the user specifies an alpha-free internal format
    if (bytesperpixelinternal > 0) {
        unsigned char *tempbuf = malloc(width * height * bytesperpixelinternal);

        if (format == GL_RGB) {
            conv_rgb_to_rgb565((unsigned char *)data, tempbuf, width, height);
        } else if (format == GL_RGBA) {
            if (internalFormat == GL_RGB) {
                conv_rgba_to_rgb565((unsigned char *)data, tempbuf, width, height);
            } else if (internalFormat == GL_RGBA) {
                memcpy(tempbuf, (unsigned char *)data, width * height * 4);
            } else if (internalFormat == GL_LUMINANCE_ALPHA) {
                conv_rgba_to_luminance_alpha((unsigned char *)data, tempbuf, width, height);
            }
        } else if (format == GL_LUMINANCE_ALPHA) {
            if (internalFormat == GL_RGB) {
                // TODO
            } else if (internalFormat == GL_LUMINANCE_ALPHA) {
                memcpy(tempbuf, (unsigned char *)data, width * height * 2);
            }
        }

        // Swap R<->B if necessary
        if (needswap && internalFormat != GL_LUMINANCE_ALPHA) {
            if (bytesperpixelinternal == 4)
                swap_rgba(tempbuf, width * height);
            else
                swap_rgb565((unsigned short *)tempbuf, width * height);
        }

        // Calculate the offset and address of the mipmap
        int offset = calc_mipmap_offset(level, currtex->w, currtex->h, currtex->bytespp);
        unsigned char *dst_addr = currtex->data;
        dst_addr += offset;

        // Finally write to the dest. buffer scrambling the data
        if (bytesperpixelinternal == 4) {
            scramble_4b(tempbuf, dst_addr, width, height);
        } else {
            scramble_2b((unsigned short *)tempbuf, dst_addr, width, height);
        }
        free(tempbuf);

        DCFlushRange(dst_addr, width * height * bytesperpixelinternal);
    } else {
        // Compressed texture

        // Calculate the offset and address of the mipmap
        int offset = calc_mipmap_offset(level, currtex->w, currtex->h, currtex->bytespp);
        unsigned char *dst_addr = currtex->data;
        dst_addr += offset;

        _ogx_convert_rgb_image_to_DXT1((unsigned char *)data, dst_addr,
                                       width, height, needswap);

        DCFlushRange(dst_addr, calc_memory(width, height, bytesperpixelinternal));
    }

    // Slow but necessary! The new textures may be in the same region of some old cached textures
    GX_InvalidateTexAll();

    if (internalFormat == GL_RGBA) {
        GX_InitTexObj(&currtex->texobj, currtex->data,
                      currtex->w, currtex->h, GX_TF_RGBA8, currtex->wraps, currtex->wrapt, GX_TRUE);
    } else if (internalFormat == GL_RGB) {
        GX_InitTexObj(&currtex->texobj, currtex->data,
                      currtex->w, currtex->h, GX_TF_RGB565, currtex->wraps, currtex->wrapt, GX_TRUE);
    } else if (internalFormat == GL_LUMINANCE_ALPHA) {
        GX_InitTexObj(&currtex->texobj, currtex->data,
                      currtex->w, currtex->h, GX_TF_IA8, currtex->wraps, currtex->wrapt, GX_TRUE);
    } else {
        GX_InitTexObj(&currtex->texobj, currtex->data,
                      currtex->w, currtex->h, GX_TF_CMPR, currtex->wraps, currtex->wrapt, GX_TRUE);
    }
    GX_InitTexObjLOD(&currtex->texobj, GX_LIN_MIP_LIN, GX_LIN_MIP_LIN, currtex->minlevel, currtex->maxlevel, 0, GX_ENABLE, GX_ENABLE, GX_ANISO_1);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    if ((red | green | blue | alpha) != 0)
        GX_SetColorUpdate(GX_TRUE);
    else
        GX_SetColorUpdate(GX_FALSE);
}

/*

  Render setup code.

*/

void glDisableClientState(GLenum cap)
{
    switch (cap) {
    case GL_COLOR_ARRAY:
        glparamstate.color_enabled = 0;
        break;
    case GL_INDEX_ARRAY:
        glparamstate.index_enabled = 0;
        break;
    case GL_NORMAL_ARRAY:
        glparamstate.normal_enabled = 0;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        glparamstate.texcoord_enabled = 0;
        break;
    case GL_VERTEX_ARRAY:
        glparamstate.vertex_enabled = 0;
        break;
    case GL_EDGE_FLAG_ARRAY:
    case GL_FOG_COORD_ARRAY:
    case GL_SECONDARY_COLOR_ARRAY:
        return;
    }
}
void glEnableClientState(GLenum cap)
{
    switch (cap) {
    case GL_COLOR_ARRAY:
        glparamstate.color_enabled = 1;
        break;
    case GL_INDEX_ARRAY:
        glparamstate.index_enabled = 1;
        break;
    case GL_NORMAL_ARRAY:
        glparamstate.normal_enabled = 1;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        glparamstate.texcoord_enabled = 1;
        break;
    case GL_VERTEX_ARRAY:
        glparamstate.vertex_enabled = 1;
        break;
    case GL_EDGE_FLAG_ARRAY:
    case GL_FOG_COORD_ARRAY:
    case GL_SECONDARY_COLOR_ARRAY:
        return;
    }
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    glparamstate.vertex_array = (float *)pointer;
    glparamstate.vertex_stride = stride;
    if (stride == 0)
        glparamstate.vertex_stride = size;
}
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    glparamstate.normal_array = (float *)pointer;
    glparamstate.normal_stride = stride;
    if (stride == 0)
        glparamstate.normal_stride = 3;
}

void glColorPointer(GLint size, GLenum type,
                    GLsizei stride, const GLvoid *pointer)
{
    glparamstate.color_array = (float *)pointer;
    glparamstate.color_stride = stride;
    if (stride == 0)
        glparamstate.color_stride = size;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    glparamstate.texcoord_array = (float *)pointer;
    glparamstate.texcoord_stride = stride;
    if (stride == 0)
        glparamstate.texcoord_stride = size;
}

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    glparamstate.vertex_array = (float *)pointer;
    glparamstate.normal_array = (float *)pointer;
    glparamstate.texcoord_array = (float *)pointer;
    glparamstate.color_array = (float *)pointer;

    glparamstate.index_enabled = 0;
    glparamstate.normal_enabled = 0;
    glparamstate.texcoord_enabled = 0;
    glparamstate.vertex_enabled = 0;
    glparamstate.color_enabled = 0;

    int cstride = 0;
    switch (format) {
    case GL_V2F:
        glparamstate.vertex_enabled = 1;
        cstride = 2;
        break;
    case GL_V3F:
        glparamstate.vertex_enabled = 1;
        cstride = 3;
        break;
    case GL_N3F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.normal_enabled = 1;
        cstride = 6;
        glparamstate.vertex_array += 3;
        break;
    case GL_T2F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.texcoord_enabled = 1;
        cstride = 5;
        glparamstate.vertex_array += 2;
        break;
    case GL_T2F_N3F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.normal_enabled = 1;
        glparamstate.texcoord_enabled = 1;
        cstride = 8;

        glparamstate.vertex_array += 5;
        glparamstate.normal_array += 2;
        break;

    case GL_C4F_N3F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.normal_enabled = 1;
        glparamstate.color_enabled = 1;
        cstride = 10;

        glparamstate.vertex_array += 7;
        glparamstate.normal_array += 4;
        break;
    case GL_C3F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.color_enabled = 1;
        cstride = 6;

        glparamstate.vertex_array += 3;
        break;
    case GL_T2F_C3F_V3F:
        glparamstate.vertex_enabled = 1;
        glparamstate.color_enabled = 1;
        glparamstate.texcoord_enabled = 1;
        cstride = 8;

        glparamstate.vertex_array += 5;
        glparamstate.color_array += 2;
        break;
    case GL_T2F_C4F_N3F_V3F: // Complete type
        glparamstate.vertex_enabled = 1;
        glparamstate.normal_enabled = 1;
        glparamstate.color_enabled = 1;
        glparamstate.texcoord_enabled = 1;
        cstride = 12;

        glparamstate.vertex_array += 9;
        glparamstate.normal_array += 6;
        glparamstate.color_array += 2;
        break;

    case GL_C4UB_V2F:
    case GL_C4UB_V3F:
    case GL_T2F_C4UB_V3F:
    case GL_T4F_C4F_N3F_V4F:
    case GL_T4F_V4F:
        // TODO: Implement T4F! And UB color!
        return;
    }

    // If the stride is 0, they're tighly packed
    if (stride != 0)
        cstride = stride;

    glparamstate.color_stride = cstride;
    glparamstate.normal_stride = cstride;
    glparamstate.vertex_stride = cstride;
    glparamstate.texcoord_stride = cstride;
}

/*

  Render code. All the renderer calls should end calling this one.

*/

/*****************************************************

        LIGHTING IMPLEMENTATION EXPLAINED

   GX differs in some aspects from OGL lighting.
    - It shares the same material for ambient
      and diffuse components
    - Lights can be specular or diffuse, not both
    - The ambient component is NOT attenuated by
      distance

   GX hardware can do lights with:
    - Distance based attenuation
    - Angle based attenuation (for diffuse lights)

   We simulate each light this way:

    - Ambient: Using distance based attenuation, disabling
      angle-based attenuation (GX_DF_NONE).
    - Diffuse: Using distance based attenuation, enabling
      angle-based attenuation in clamp mode (GX_DF_CLAMP)
    - Specular: Specular based attenuation (GX_AF_SPEC)

   As each channel is configured for all the TEV stages
   we CANNOT emulate the three types of light at once.
   So we emulate two types only.

   For unlit scenes the setup is:
     - TEV 0: Modulate vertex color with texture
              Speed hack: use constant register
              If no tex, just pass color
   For ambient+diffuse lights:
     - TEV 0: Pass RAS0 color with material color
          set to vertex color (to modulate vert color).
          Set the ambient value for this channel to 0.
         Speed hack: Use material register for constant
          color
     - TEV 1: Sum RAS1 color with material color
          set to vertex color (to modulate vert color)
          to the previous value. Also set the ambient
          value to the global ambient value.
         Speed hack: Use material register for constant
          color
     - TEV 2: If texture is enabled multiply the texture
          rasterized color with the previous value.
      The result is:

     Color = TexC * (VertColor*AmbientLightColor*Atten
      + VertColor*DiffuseLightColor*Atten*DifAtten)

     As we use the material register for vertex color
     the material colors will be multiplied with the
     light color and uploaded as light color.

     We'll be using 0-3 lights for ambient and 4-7 lights
     for diffuse

******************************************************/

static inline bool is_black(const float *color)
{
    return color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f;
}

static void allocate_lights()
{
    /* For the time being, just allocate the lights using a first come, first
     * served algorithm.
     * TODO: take the light impact into account: privilege stronger lights, and
     * light types in this order (probably): directional, ambient, diffuse,
     * specular. */
    char lights_needed = 0;
    bool global_ambient_off = is_black(glparamstate.lighting.globalambient);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (!glparamstate.lighting.lights[i].enabled)
            continue;

        if (!is_black(glparamstate.lighting.lights[i].ambient_color) &&
            !global_ambient_off) {
            /* This ambient light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_ambient =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_ambient = -1;
        }

        if (!is_black(glparamstate.lighting.lights[i].diffuse_color)) {
            /* This diffuse light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_diffuse =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_diffuse = -1;
        }

        /* GX support specular light only for directional light sources. For
         * this reason we enable the specular light only if the "w" component
         * of the position is 0. */
        if (!is_black(glparamstate.lighting.lights[i].specular_color) &&
            !is_black(glparamstate.lighting.matspecular) &&
            glparamstate.lighting.matshininess > 0.0 &&
            glparamstate.lighting.lights[i].position[3] == 0.0f) {
            /* This specular light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_specular =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_specular = -1;
        }
    }

    if (lights_needed > MAX_GX_LIGHTS) {
        warning("Excluded %d lights since max is 8", lights_needed - MAX_GX_LIGHTS);
    }
}

static LightMasks prepare_lighting()
{
    LightMasks masks = { 0, 0 };
    int i;

    allocate_lights();

    for (i = 0; i < MAX_LIGHTS; i++) {
        if (!glparamstate.lighting.lights[i].enabled)
            continue;

        int8_t gx_ambient_idx = glparamstate.lighting.lights[i].gx_ambient;
        int8_t gx_diffuse_idx = glparamstate.lighting.lights[i].gx_diffuse;
        int8_t gx_specular_idx = glparamstate.lighting.lights[i].gx_specular;
        GXLightObj *gx_ambient = gx_ambient_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_ambient_idx] : NULL;
        GXLightObj *gx_diffuse = gx_diffuse_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_diffuse_idx] : NULL;
        GXLightObj *gx_specular = gx_specular_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_specular_idx] : NULL;

        if (gx_ambient) {
            // Multiply the light color by the material color and set as light color
            GXColor amb_col = gxcol_new_fv(glparamstate.lighting.lights[i].ambient_color);
            GX_InitLightColor(gx_ambient, amb_col);
            GX_InitLightPosv(gx_ambient, &glparamstate.lighting.lights[i].position[0]);
        }

        if (gx_diffuse) {
            GXColor diff_col = gxcol_new_fv(glparamstate.lighting.lights[i].diffuse_color);
            GX_InitLightColor(gx_diffuse, diff_col);
            GX_InitLightPosv(gx_diffuse, &glparamstate.lighting.lights[i].position[0]);
        }

        // FIXME: Need to consider spotlights
        if (glparamstate.lighting.lights[i].position[3] == 0) {
            // Directional light, it's a point light very far without attenuation
            if (gx_ambient) {
                GX_InitLightAttn(gx_ambient, 1, 0, 0, 1, 0, 0);
            }
            if (gx_diffuse) {
                GX_InitLightAttn(gx_diffuse, 1, 0, 0, 1, 0, 0);
            }
            if (gx_specular) {
                GXColor spec_col = gxcol_new_fv(glparamstate.lighting.lights[i].specular_color);

                /* We need to compute the normals of the direction */
                float normal[3] = {
                    -glparamstate.lighting.lights[i].position[0],
                    -glparamstate.lighting.lights[i].position[1],
                    -glparamstate.lighting.lights[i].position[2],
                };
                normalize(normal);
                GX_InitSpecularDirv(gx_specular, normal);
                GX_InitLightShininess(gx_specular, glparamstate.lighting.matshininess);
                GX_InitLightColor(gx_specular, spec_col);
            }
        } else {
            // Point light
            if (gx_ambient) {
                GX_InitLightAttn(gx_ambient, 1, 0, 0,
                                 glparamstate.lighting.lights[i].atten[0],
                                 glparamstate.lighting.lights[i].atten[1],
                                 glparamstate.lighting.lights[i].atten[2]);
                GX_InitLightDir(gx_ambient, 0, -1, 0);
            }
            if (gx_diffuse) {
                GX_InitLightAttn(gx_diffuse, 1, 0, 0,
                                 glparamstate.lighting.lights[i].atten[0],
                                 glparamstate.lighting.lights[i].atten[1],
                                 glparamstate.lighting.lights[i].atten[2]);
                GX_InitLightDir(gx_diffuse, 0, -1, 0);
            }
        }

        if (gx_ambient) {
            GX_LoadLightObj(gx_ambient, 1 << gx_ambient_idx);
            masks.ambient_mask |= (1 << gx_ambient_idx);
        }
        if (gx_diffuse) {
            GX_LoadLightObj(gx_diffuse, 1 << gx_diffuse_idx);
            masks.diffuse_mask |= (1 << gx_diffuse_idx);
        }
        if (gx_specular) {
            GX_LoadLightObj(gx_specular, 1 << gx_specular_idx);
            masks.specular_mask |= (1 << gx_specular_idx);
        }
    }
    debug("Ambient mask 0x%02x, diffuse 0x%02x, specular 0x%02x",
          masks.ambient_mask, masks.diffuse_mask, masks.specular_mask);
    return masks;
}

static unsigned char draw_mode(GLenum mode)
{
    unsigned char gxmode;
    switch (mode) {
    case GL_POINTS:
        gxmode = GX_POINTS;
        break;
    case GL_LINE_LOOP:
    case GL_LINE_STRIP:
        gxmode = GX_LINESTRIP;
        break;
    case GL_LINES:
        gxmode = GX_LINES;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        gxmode = GX_TRIANGLESTRIP;
        break;
    case GL_TRIANGLE_FAN:
        gxmode = GX_TRIANGLEFAN;
        break;
    case GL_TRIANGLES:
        gxmode = GX_TRIANGLES;
        break;
    case GL_QUADS:
        gxmode = GX_QUADS;
        break;

    case GL_POLYGON:
    default:
        return 0xff; // FIXME: Emulate these modes
    }
    return gxmode;
}

static void setup_fog()
{
    u8 mode, proj_type;
    GXColor color;
    float start, end, near, far;

    /* GX_SetFog() works differently from OpenGL:
     * 1. It requires the caller to pass the near and far coordinates
     * 2. It applies the "start" and "end" parameters to all curve types
     *    (OpenGL only uses them for linear fogging)
     * 3. It does not support the "density" parameter
     */

    if (glparamstate.fog.enabled) {
        get_projection_info(&proj_type, &near, &far);

        color = gxcol_new_fv(glparamstate.fog.color);
        switch (glparamstate.fog.mode) {
        case GL_EXP: mode = GX_FOG_EXP; break;
        case GL_EXP2: mode = GX_FOG_EXP2; break;
        case GL_LINEAR: mode = GX_FOG_LIN; break;
        }
        if (proj_type == GX_ORTHOGRAPHIC)
            mode += (GX_FOG_ORTHO_LIN - GX_FOG_PERSP_LIN);

        if (glparamstate.fog.mode == GL_LINEAR) {
            start = glparamstate.fog.start;
            end = glparamstate.fog.end;
        } else {
            /* Tricky part: GX spreads the exponent function so that it affects
             * the range from "start" to "end" (though it's unclear how it
             * does, since the 0 value is never actually reached), whereas
             * openGL expects it to affect the whole world, but with a "speed"
             * dictated by the "density" parameter.
             * So, we emulate the density by playing with the "end" parameter.
             * The factors used in the computations of "end" below have been
             * found empirically, comparing the result with a desktop OpenGL
             * implementation.
             */
            start = near;
            if (glparamstate.fog.density <= 0.0) {
                end = far;
            } else if (glparamstate.fog.mode == GL_EXP2) {
                end = 2.0f / glparamstate.fog.density;
            } else { /* GL_EXP */
                end = 5.0f / glparamstate.fog.density;
            }
        }
    } else {
        start = end = near = far = 0.0f;
        mode = GX_FOG_NONE;
    }
    GX_SetFog(mode, start, end, near, far, color);
}

static void setup_texture_stage(u8 stage, u8 raster_color, u8 raster_alpha,
                                u8 channel)
{
    switch (glparamstate.texture_env_mode) {
    case GL_REPLACE:
        // In data: a: Texture Color
        GX_SetTevColorIn(stage, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        GX_SetTevAlphaIn(stage, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        break;
    case GL_ADD:
        // In data: d: Texture Color a: raster value, Operation: a+d
        GX_SetTevColorIn(stage, raster_color, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GX_SetTevAlphaIn(stage, raster_alpha, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    case GL_MODULATE:
    default:
        // In data: c: Texture Color b: raster value, Operation: b*c
        GX_SetTevColorIn(stage, GX_CC_ZERO, raster_color, GX_CC_TEXC, GX_CC_ZERO);
        GX_SetTevAlphaIn(stage, GX_CA_ZERO, raster_alpha, GX_CA_TEXA, GX_CA_ZERO);
        break;
    }
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, channel);
    // Set up the data for the TEXCOORD0 (use a identity matrix, TODO: allow user texture matrices)
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
}

static void setup_render_stages(int texen)
{
    if (glparamstate.lighting.enabled) {
        LightMasks light_mask = prepare_lighting();

        GXColor color_zero = { 0, 0, 0, 0 };
        GXColor color_gamb = gxcol_new_fv(glparamstate.lighting.globalambient);

        GX_SetNumChans(2);
        GX_SetNumTevStages(2);
        GX_SetNumTexGens(0);

        unsigned char vert_color_src = GX_SRC_VTX;
        if (!glparamstate.color_enabled || !glparamstate.lighting.color_material_enabled) {
            vert_color_src = GX_SRC_REG;
            GXColor acol, dcol;
            bool ambient_set = false, diffuse_set = false;

            if (glparamstate.lighting.color_material_enabled) {
                GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);

                if (glparamstate.lighting.color_material_mode == GL_AMBIENT ||
                    glparamstate.lighting.color_material_mode == GL_AMBIENT_AND_DIFFUSE) {
                    acol = gxcol_cpy_mulfv(ccol, glparamstate.lighting.globalambient);
                    ambient_set = true;
                }

                if (glparamstate.lighting.color_material_mode == GL_DIFFUSE ||
                    glparamstate.lighting.color_material_mode == GL_AMBIENT_AND_DIFFUSE) {
                    dcol = ccol;
                    diffuse_set = true;
                }
            }
            if (!ambient_set) {
                acol = gxcol_new_fv(glparamstate.lighting.matambient);
            }
            if (!diffuse_set) {
                dcol = gxcol_new_fv(glparamstate.lighting.matdiffuse);
            }

            /* We would like to find a way to put matspecular into
             * GX_SetChanMatColor(GX_COLOR0A0), since that's the color that GX
             * combines with the specular light. But we also need this register
             * for the ambient color, which is arguably more important. */
            GX_SetChanMatColor(GX_COLOR0A0, acol);
            GX_SetChanMatColor(GX_COLOR1A1, dcol);
        }

        GXColor ecol;
        if (glparamstate.lighting.color_material_enabled &&
            glparamstate.lighting.color_material_mode == GL_EMISSION) {
            ecol = gxcol_new_fv(glparamstate.imm_mode.current_color);
        } else {
            ecol = gxcol_new_fv(glparamstate.lighting.matemission);
        };

        // Color0 channel: Multiplies the light raster result with the vertex color. Ambient is set to register (which is global ambient)
        GX_SetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, vert_color_src,
                       light_mask.ambient_mask | light_mask.specular_mask , GX_DF_NONE, GX_AF_SPEC);
        GX_SetChanAmbColor(GX_COLOR0A0, color_gamb);

        // Color1 channel: Multiplies the light raster result with the vertex color. Ambient is set to register (which is zero)
        GX_SetChanCtrl(GX_COLOR1A1, GX_TRUE, GX_SRC_REG, vert_color_src, light_mask.diffuse_mask, GX_DF_CLAMP, GX_AF_SPOT);
        GX_SetChanAmbColor(GX_COLOR1A1, color_zero);

        // STAGE 0: ambient*vert_color -> cprev
        // In data: d: Raster Color, a: emission color
        GX_SetTevColor(GX_TEVREG0, ecol);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        // Operation: Pass d
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // Select COLOR0A0 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR0A0);

        // STAGE 1: diffuse*vert_color + cprev -> cprev
        // In data: d: Raster Color a: CPREV
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_APREV, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        // Operation: Sum a + d
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // Select COLOR1A1 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR1A1);

        if (texen) {
            // Do not select any raster value, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            setup_texture_stage(GX_TEVSTAGE2, GX_CC_CPREV, GX_CA_APREV, GX_COLORNULL);
            GX_SetNumTevStages(3);
        }
    } else {
        // Unlit scene
        // TEV STAGE 0: Modulate the vertex color with the texture 0. Outputs to GX_TEVPREV
        // Optimization: If color_enabled is false (constant vertex color) use the constant color register
        // instead of using the rasterizer and emitting a color for each vertex

        // By default use rasterized data and put it a COLOR0A0
        unsigned char vertex_color_register = GX_CC_RASC;
        unsigned char vertex_alpha_register = GX_CA_RASA;
        unsigned char rasterized_color = GX_COLOR0A0;
        if (!glparamstate.color_enabled) { // No need for vertex color raster, it's constant
            // Use constant color
            vertex_color_register = GX_CC_KONST;
            vertex_alpha_register = GX_CA_KONST;
            // Select register 0 for color/alpha
            GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
            GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
            // Load the color (current GL color)
            GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);
            GX_SetTevKColor(GX_KCOLOR0, ccol);

            rasterized_color = GX_COLORNULL; // Disable vertex color rasterizer
        }

        GX_SetNumChans(1);
        GX_SetNumTevStages(1);

        // Disable lighting and output vertex color to the rasterized color
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, 0, 0);
        GX_SetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, 0, 0, 0);

        if (texen) {
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            setup_texture_stage(GX_TEVSTAGE0,
                                vertex_color_register, vertex_alpha_register,
                                rasterized_color);
        } else {
            // In data: d: Raster Color
            GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, vertex_color_register);
            GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, vertex_alpha_register);
            // Operation: Pass the color
            GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, rasterized_color);
            GX_SetNumTexGens(0);
        }
    }

    setup_fog();
}

void _ogx_apply_state()
{
    int texen = glparamstate.texcoord_enabled & glparamstate.texture_enabled;
    setup_render_stages(texen);

    // Set up the OGL state to GX state
    if (glparamstate.dirty.bits.dirty_z)
        GX_SetZMode(glparamstate.ztest, glparamstate.zfunc, glparamstate.zwrite & glparamstate.ztest);

    if (glparamstate.dirty.bits.dirty_blend) {
        if (glparamstate.blendenabled)
            GX_SetBlendMode(GX_BM_BLEND, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
        else
            GX_SetBlendMode(GX_BM_NONE, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
    }

    // Matrix stuff
    if (glparamstate.dirty.bits.dirty_matrices) {
        MODELVIEW_UPDATE
        PROJECTION_UPDATE
    }
    if (glparamstate.dirty.bits.dirty_matrices | glparamstate.dirty.bits.dirty_lighting) {
        NORMAL_UPDATE
    }

    // All the state has been transferred, no need to update it again next time
    glparamstate.dirty.all = 0;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    unsigned char gxmode = draw_mode(mode);
    if (gxmode == 0xff)
        return;

    _ogx_apply_state();

    int texen = glparamstate.texcoord_enabled & glparamstate.texture_enabled;
    int color_provide = 0;
    if (glparamstate.color_enabled &&
        (!glparamstate.lighting.enabled || glparamstate.lighting.color_material_enabled)) { // Vertex colouring
        if (glparamstate.lighting.enabled)
            color_provide = 2; // Lighting requires two color channels
        else
            color_provide = 1;
    }

    // Create data pointers
    float *ptr_pos = glparamstate.vertex_array;
    float *ptr_texc = glparamstate.texcoord_array;
    float *ptr_color = glparamstate.color_array;
    float *ptr_normal = glparamstate.normal_array;

    ptr_pos += (glparamstate.vertex_stride * first);
    ptr_texc += (glparamstate.texcoord_stride * first);
    ptr_color += (glparamstate.color_stride * first);
    ptr_normal += (glparamstate.normal_stride * first);

    // Not using indices
    GX_ClearVtxDesc();
    if (glparamstate.vertex_enabled)
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (glparamstate.normal_enabled)
        GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    if (color_provide)
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (color_provide == 2)
        GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    if (texen)
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    // Using floats
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);

    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    bool loop = (mode == GL_LINE_LOOP);
    GX_Begin(gxmode, GX_VTXFMT0, count + loop);

    if (glparamstate.normal_enabled && !glparamstate.color_enabled) {
        if (texen) {
            draw_arrays_pos_normal_texc(ptr_pos, ptr_texc, ptr_normal, count, loop);
        } else {
            draw_arrays_pos_normal(ptr_pos, ptr_normal, count, loop);
        }
    } else {
        draw_arrays_general(ptr_pos, ptr_normal, ptr_texc, ptr_color,
                            count, glparamstate.normal_enabled, color_provide, texen, loop);
    }
    GX_End();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{

    unsigned char gxmode = draw_mode(mode);
    if (gxmode == 0xff)
        return;

    _ogx_apply_state();

    int texen = glparamstate.texcoord_enabled & glparamstate.texture_enabled;
    int color_provide = 0;
    if (glparamstate.color_enabled &&
        (!glparamstate.lighting.enabled || glparamstate.lighting.color_material_enabled)) { // Vertex colouring
        if (glparamstate.lighting.enabled)
            color_provide = 2; // Lighting requires two color channels
        else
            color_provide = 1;
    }

    // Create data pointers
    unsigned short *ind = (unsigned short *)indices;

    // Not using indices
    GX_ClearVtxDesc();
    if (glparamstate.vertex_enabled)
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (glparamstate.normal_enabled)
        GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    if (color_provide)
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (color_provide == 2)
        GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    if (texen)
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    // Using floats
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);

    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    bool loop = (mode == GL_LINE_LOOP);
    GX_Begin(gxmode, GX_VTXFMT0, count + loop);
    int i;
    for (i = 0; i < count + loop; i++) {
        int index = ind[i % count];
        float *ptr_pos = glparamstate.vertex_array + glparamstate.vertex_stride * index;
        float *ptr_texc = glparamstate.texcoord_array + glparamstate.texcoord_stride * index;
        float *ptr_color = glparamstate.color_array + glparamstate.color_stride * index;
        float *ptr_normal = glparamstate.normal_array + glparamstate.normal_stride * index;

        GX_Position3f32(ptr_pos[0], ptr_pos[1], ptr_pos[2]);

        if (glparamstate.normal_enabled) {
            GX_Normal3f32(ptr_normal[0], ptr_normal[1], ptr_normal[2]);
        }

        // If the data stream doesn't contain any color data just
        // send the current color (the last glColor* call)
        if (color_provide) {
            unsigned char arr[4] = { ptr_color[0] * 255.0f, ptr_color[1] * 255.0f, ptr_color[2] * 255.0f, ptr_color[3] * 255.0f };
            GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
            if (color_provide == 2)
                GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
        }

        if (texen) {
            GX_TexCoord2f32(ptr_texc[0], ptr_texc[1]);
        }
    }
    GX_End();
}

static void draw_arrays_pos_normal_texc(float *ptr_pos, float *ptr_texc, float *ptr_normal,
                                        int count, bool loop)
{
    int i;
    float *pos = ptr_pos, *texc = ptr_texc, *normal = ptr_normal;
    for (i = 0; i < count; i++) {
        GX_Position3f32(ptr_pos[0], ptr_pos[1], ptr_pos[2]);
        ptr_pos += glparamstate.vertex_stride;

        GX_Normal3f32(ptr_normal[0], ptr_normal[1], ptr_normal[2]);
        ptr_normal += glparamstate.normal_stride;

        GX_TexCoord2f32(ptr_texc[0], ptr_texc[1]);
        ptr_texc += glparamstate.texcoord_stride;
    }
    if (loop) {
        GX_Position3f32(pos[0], pos[1], pos[2]);
        GX_Normal3f32(normal[0], normal[1], normal[2]);
        GX_TexCoord2f32(texc[0], texc[1]);
    }
}

static void draw_arrays_pos_normal(float *ptr_pos, float *ptr_normal, int count,
                                   bool loop)
{
    int i;
    float *pos = ptr_pos, *normal = ptr_normal;
    for (i = 0; i < count; i++) {
        GX_Position3f32(ptr_pos[0], ptr_pos[1], ptr_pos[2]);
        ptr_pos += glparamstate.vertex_stride;

        GX_Normal3f32(ptr_normal[0], ptr_normal[1], ptr_normal[2]);
        ptr_normal += glparamstate.normal_stride;
    }
    if (loop) {
        GX_Position3f32(pos[0], pos[1], pos[2]);
        GX_Normal3f32(normal[0], normal[1], normal[2]);
    }
}

static void draw_arrays_general(float *ptr_pos, float *ptr_normal, float *ptr_texc, float *ptr_color,
                                int count, int ne, int color_provide, int texen, bool loop)
{

    int i;
    for (i = 0; i < count + loop; i++) {
        int j = i % count;
        float *pos = ptr_pos + j * glparamstate.vertex_stride;
        GX_Position3f32(pos[0], pos[1], pos[2]);

        if (ne) {
            float *normal = ptr_normal + j * glparamstate.normal_stride;
            GX_Normal3f32(normal[0], normal[1], normal[2]);
        }

        // If the data stream doesn't contain any color data just
        // send the current color (the last glColor* call)
        if (color_provide) {
            float *color = ptr_color + j * glparamstate.color_stride;
            unsigned char arr[4] = { color[0] * 255.0f, color[1] * 255.0f, color[2] * 255.0f, color[3] * 255.0f };
            GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
            if (color_provide == 2)
                GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
        }

        if (texen) {
            float *texc = ptr_texc + j * glparamstate.texcoord_stride;
            GX_TexCoord2f32(texc[0], texc[1]);
        }
    }
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
               GLdouble near, GLdouble far)
{
    Mtx44 mt;
    f32 tmp;

    tmp = 1.0f / (right - left);
    mt[0][0] = (2 * near) * tmp;
    mt[0][1] = 0.0f;
    mt[0][2] = (right + left) * tmp;
    mt[0][3] = 0.0f;
    tmp = 1.0f / (top - bottom);
    mt[1][0] = 0.0f;
    mt[1][1] = (2 * near) * tmp;
    mt[1][2] = (top + bottom) * tmp;
    mt[1][3] = 0.0f;
    tmp = 1.0f / (far - near);
    mt[2][0] = 0.0f;
    mt[2][1] = 0.0f;
    mt[2][2] = -near * tmp;
    mt[2][3] = -(far * near) * tmp;
    mt[3][0] = 0.0f;
    mt[3][1] = 0.0f;
    mt[3][2] = -1.0f;
    mt[3][3] = 0.0f;

    glMultMatrixf((float *)mt);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val)
{
    Mtx44 newmat;
    // Same as GX's guOrtho, but transposed
    float x = (left + right) / (left - right);
    float y = (bottom + top) / (bottom - top);
    float z = far_val / (near_val - far_val);
    newmat[0][0] = 2.0f / (right - left);
    newmat[0][1] = 0.0f;
    newmat[0][2] = 0.0f;
    newmat[0][3] = 0;
    newmat[1][0] = 0.0f;
    newmat[1][1] = 2.0f / (top - bottom);
    newmat[1][2] = 0.0f;
    newmat[1][3] = 0;
    newmat[2][0] = 0.0f;
    newmat[2][1] = 0.0f;
    newmat[2][2] = 1.0f / (near_val - far_val);
    newmat[2][3] = 0;
    newmat[3][0] = x;
    newmat[3][1] = y;
    newmat[3][2] = z;
    newmat[3][3] = 1.0f;

    glMultMatrixf((float *)newmat);
}
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    GLfloat m[4][4];
    GLfloat sine, cotangent, deltaZ;
    GLfloat radians = fovy * (float)(1.0f / 2.0f * M_PI / 180.0f);

    deltaZ = zFar - zNear;
    sine = sinf(radians);
    if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
        return;

    cotangent = cosf(radians) / sine;

    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[3][0] = 0;
    m[3][1] = 0;
    m[0][0] = cotangent / aspect;
    m[1][1] = cotangent;
    m[2][2] = -(0 + zNear) / deltaZ;
    m[2][3] = -1;
    m[3][2] = -zNear * zFar / deltaZ;
    m[3][3] = 0;

    glMultMatrixf((float *)m);
}

void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
    glOrtho(left, right, bottom, top, -1, 1);
}

void gluLookAt(GLdouble eyex, GLdouble eyey, GLdouble eyez, GLdouble centerx,
               GLdouble centery, GLdouble centerz, GLdouble upx, GLdouble upy, GLdouble upz)
{

    GLfloat forward[3], side[3], up[3];
    GLfloat m[4][4];

    forward[0] = centerx - eyex;
    forward[1] = centery - eyey;
    forward[2] = centerz - eyez;

    up[0] = upx;
    up[1] = upy;
    up[2] = upz;

    normalize(forward);
    cross(forward, up, side);
    normalize(side);
    cross(side, forward, up);

    m[0][3] = 0;
    m[1][3] = 0;
    m[2][3] = 0;
    m[3][0] = 0;
    m[3][1] = 0;
    m[3][2] = 0;
    m[3][3] = 1;
    m[0][0] = side[0];
    m[1][0] = side[1];
    m[2][0] = side[2];
    m[0][1] = up[0];
    m[1][1] = up[1];
    m[2][1] = up[2];
    m[0][2] = -forward[0];
    m[1][2] = -forward[1];
    m[2][2] = -forward[2];

    glMultMatrixf(&m[0][0]);
    glTranslatef(-eyex, -eyey, -eyez);
}

// Always returns error!
GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data)
{
    int level = 0;
    int bpp = 4;
    if (format == GL_RGB || format == GL_BGR) {
        bpp = 3;
    }
    unsigned char *buf = malloc(width * height * bpp);
    int w = width, h = height;

    while (w > 1 && h > 1) {
        gluScaleImage(format, width, height, type, data, w, h, type, buf);
        glTexImage2D(target, level, internalFormat, w, h, 0, format, type, buf);

        if (w > 1)
            w /= 2;
        if (h > 1)
            h /= 2;
        level++;
    }
    free(buf);
    return 0;
}

GLint gluScaleImage(GLenum format, GLsizei wIn, GLsizei hIn, GLenum typeIn,
                    const void *dataIn, GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid *dataOut)
{
    switch (format) {
    case GL_RGB:
    case GL_BGR:
        scale_internal(3, wIn, hIn, dataIn, wOut, hOut, dataOut);
        return 0;
    case GL_RGBA:
    case GL_BGRA:
        scale_internal(4, wIn, hIn, dataIn, wOut, hOut, dataOut);
        return 0;
    };
    return -1;
}

// NOT GOING TO IMPLEMENT

void glBlendEquation(GLenum mode) {}
void glClearStencil(GLint s) {}
void glStencilMask(GLuint mask) {} // Should use Alpha testing to achieve similar results
void glShadeModel(GLenum mode) {}  // In theory we don't have GX equivalent?
void glHint(GLenum target, GLenum mode) {}

static unsigned char gcgl_texwrap_conv(GLint param)
{
    switch (param) {
    case GL_MIRRORED_REPEAT:
        return GX_MIRROR;
    case GL_CLAMP:
        return GX_CLAMP;
    case GL_REPEAT:
    default:
        return GX_REPEAT;
    };
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    /* For the time being, all the parameters we support take integer values */
    glTexParameteri(target, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_2D)
        return;

    gltexture_ *currtex = &texture_list[glparamstate.glcurtex];

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        currtex->wraps = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, currtex->wraps, currtex->wrapt);
        break;
    case GL_TEXTURE_WRAP_T:
        texture_list[glparamstate.glcurtex].wrapt = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, currtex->wraps, currtex->wrapt);
        break;
    };
}

// XXX: Need to finish glGets, important!!!
void glGetIntegerv(GLenum pname, GLint *params)
{
    switch (pname) {
    case GL_MAX_TEXTURE_SIZE:
        *params = 1024;
        return;
    case GL_MODELVIEW_STACK_DEPTH:
        *params = MAX_MODV_STACK;
        return;
    case GL_PROJECTION_STACK_DEPTH:
        *params = MAX_PROJ_STACK;
        return;
    default:
        return;
    };
}
void glGetFloatv(GLenum pname, GLfloat *params)
{
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
        memcpy(params, glparamstate.modelview_matrix, sizeof(float) * 16);
        return;
    case GL_PROJECTION_MATRIX:
        memcpy(params, glparamstate.projection_matrix, sizeof(float) * 16);
        return;
    default:
        return;
    };
}

// TODO STUB IMPLEMENTATION

const GLubyte *glGetString(GLenum name) { return gl_null_string; }
void glLightModelf(GLenum pname, GLfloat param) {}
void glPushAttrib(GLbitfield mask) {}
void glPopAttrib(void) {}
void glPolygonMode(GLenum face, GLenum mode) {}
void glReadBuffer(GLenum mode) {}
void glPixelStorei(GLenum pname, GLint param) {}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {}
void glAlphaFunc(GLenum func, GLclampf ref) {} // We need a TEVSTAGE for comparing and discarding pixels by alpha value

/*
 ****** NOTES ******

 Front face definition is reversed. CCW is front for OpenGL
 while front facing is defined CW in GX.

 This implementation ONLY supports floats for vertexs, texcoords
 and normals. Support for different types is not implemented as
 GX does only support floats. Simple conversion would be needed.

*/

/************* AUXILIAR FUNCTIONS **************/

// Discards alpha and fits the texture in 16 bits
static void conv_rgba_to_rgb565(unsigned char *src, void *dst, const unsigned int width, const unsigned int height)
{
    int numpixels = width * height;
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | ((src[2] >> 3));
        src += 4;
    }
}
// Fits the texture in 16 bits
static void conv_rgb_to_rgb565(unsigned char *src, void *dst, const unsigned int width, const unsigned int height)
{
    int numpixels = width * height;
    unsigned short *out = dst;
    while (numpixels--) {
        *out++ = ((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | ((src[2] >> 3));
        src += 3;
    }
}
// Converts color into luminance and saves alpha
static void conv_rgba_to_luminance_alpha(unsigned char *src, void *dst, const unsigned int width, const unsigned int height)
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
// 2b texel scrambling
static void scramble_2b(unsigned short *src, void *dst, const unsigned int width, const unsigned int height)
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
static void scramble_4b(unsigned char *src, void *dst, const unsigned int width, const unsigned int height)
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

static void swap_rgba(unsigned char *pixels, int num_pixels)
{
    while (num_pixels--) {
        unsigned char temp;
        temp = pixels[0];
        pixels[0] = pixels[3];
        pixels[3] = temp;
        pixels += 4;
    }
}
static void swap_rgb565(unsigned short *pixels, int num_pixels)
{
    while (num_pixels--) {
        unsigned int b = *pixels & 0x1F;
        unsigned int r = (*pixels >> 11) & 0x1F;
        unsigned int g = (*pixels >> 5) & 0x3F;
        *pixels++ = (b << 11) | (g << 5) | r;
    }
}

//// Image scaling for arbitrary size taken from Mesa 3D and adapted by davidgf ////

static void scale_internal(int components, int widthin, int heightin,
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
