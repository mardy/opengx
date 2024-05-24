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

#include "call_lists.h"
#include "debug.h"
#include "image_DXT.h"
#include "pixels.h"
#include "state.h"
#include "utils.h"

#include <malloc.h>

#define TEXTURE_IS_USED(texture) \
    (GX_GetTexObjData(&texture.texobj) != NULL)
#define TEXTURE_IS_RESERVED(texture) \
    (GX_GetTexObjUserData(&texture.texobj) == (void*)1)
#define TEXTURE_RESERVE(texture) \
    GX_InitTexObjUserData(&(texture).texobj, (void*)1)

typedef struct {
    void *texels;
    uint16_t width, height;
    uint8_t format, wraps, wrapt, mipmap;
    uint8_t minlevel, maxlevel;
} TextureInfo;

static uint32_t calc_memory(int w, int h, uint32_t format)
{
    return GX_GetTexBufferSize(w, h, format, GX_FALSE, 0);
}

// Returns the number of bytes required to store a texture with all its bitmaps
static uint32_t calc_tex_size(int w, int h, uint32_t format)
{
    return GX_GetTexBufferSize(w, h, format, GX_TRUE, 20);
}
// Deduce the original texture size given the current size and level
static int calc_original_size(int level, int s)
{
    while (level > 0) {
        s = 2 * s;
        level--;
    }
    return s;
}
// Given w,h,level,and bpp, returns the offset to the mipmap at level "level"
static uint32_t calc_mipmap_offset(int level, int w, int h, uint32_t format)
{
    return level > 0 ?
        GX_GetTexBufferSize(w, h, format, GX_TRUE, level - 1) : 0;
}

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

static void texture_get_info(const GXTexObj *obj, TextureInfo *info)
{
    GX_GetTexObjAll(obj, &info->texels,
                    &info->width, &info->height,
                    &info->format,
                    &info->wraps, &info->wrapt,
                    &info->mipmap);
    if (info->texels) {
        info->texels = MEM_PHYSICAL_TO_K0(info->texels);
    }

    float minlevel, maxlevel;
    GX_GetTexObjLOD(obj, &minlevel, &maxlevel);
    info->minlevel = minlevel;
    info->maxlevel = maxlevel;
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
    u8 wraps, wrapt;

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        wrapt = GX_GetTexObjWrapT(&currtex->texobj);
        wraps = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, wraps, wrapt);
        break;
    case GL_TEXTURE_WRAP_T:
        wraps = GX_GetTexObjWrapS(&currtex->texobj);
        wrapt = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, wraps, wrapt);
        break;
    };
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    /* For the time being, all the parameters we support take integer values */
    glTexEnvi(target, pname, param);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    HANDLE_CALL_LIST(TEX_ENV, target, pname, param);

    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        glparamstate.texture_env_mode = param;
        break;
    }
}

void glTexImage1D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLint border, GLenum format, GLenum type,
                  const GLvoid *pixels)
{
    glTexImage2D(GL_TEXTURE_2D, level, internalFormat, width, 1,
                 border, format, type, pixels);
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *data)
{

    // Initial checks
    if (!TEXTURE_IS_RESERVED(texture_list[glparamstate.glcurtex]))
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

    uint32_t gx_format;
    if (internalFormat == GL_RGB) {
        gx_format = GX_TF_RGB565;
    } else if (internalFormat == GL_RGBA) {
        gx_format = GX_TF_RGBA8;
    } else if (internalFormat == GL_LUMINANCE_ALPHA) {
        gx_format = GX_TF_IA8;
    } else if (internalFormat == GL_LUMINANCE) {
        gx_format = GX_TF_I8;
    } else if (internalFormat == GL_ALPHA) {
        gx_format = GX_TF_A8; /* Note, we won't be really passing this to GX */
    } else {
        gx_format = GX_TF_CMPR;
    }

    if (gx_format == GX_TF_CMPR && (width < 8 || height < 8))
        return; // Cannot take compressed textures under 8x8 (4 blocks of 4x4, 32B)

    // We *may* need to delete and create a new texture, depending if the user wants to add some mipmap levels
    // or wants to create a new texture from scratch
    int wi = calc_original_size(level, width);
    int he = calc_original_size(level, height);

    TextureInfo ti;
    texture_get_info(&currtex->texobj, &ti);
    char onelevel = ti.minlevel == 0.0;

    // Check if the texture has changed its geometry and proceed to delete it
    // If the specified level is zero, create a onelevel texture to save memory
    if (wi != ti.width || he != ti.height) {
        if (ti.texels != 0)
            free(ti.texels);
        if (level == 0) {
            uint32_t required_size = calc_memory(width, height, gx_format);
            ti.texels = memalign(32, required_size);
            onelevel = 1;
        } else {
            uint32_t required_size = calc_tex_size(wi, he, gx_format);
            ti.texels = memalign(32, required_size);
            onelevel = 0;
        }
        ti.minlevel = level;
        ti.maxlevel = level;
        ti.width = wi;
        ti.height = he;
    }
    if (ti.maxlevel < level)
        ti.maxlevel = level;
    if (ti.minlevel > level)
        ti.minlevel = level;

    if (onelevel == 1 && level != 0) {
        // We allocated a onelevel texture (base level 0) but now
        // we are uploading a non-zero level, so we need to create a mipmap capable buffer
        // and copy the level zero texture
        uint32_t tsize = calc_memory(wi, he, gx_format);
        unsigned char *tempbuf = malloc(tsize);
        if (!tempbuf) {
            warning("Failed to allocate memory for texture mipmap (%d)", errno);
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
        memcpy(tempbuf, ti.texels, tsize);
        free(ti.texels);

        uint32_t required_size = calc_tex_size(wi, he, gx_format);
        ti.texels = memalign(32, required_size);
        onelevel = 0;

        memcpy(ti.texels, tempbuf, tsize);
        free(tempbuf);
    }

    unsigned char *dst_addr = ti.texels;
    // Inconditionally convert to 565 all inputs without alpha channel
    // Alpha inputs may be stripped if the user specifies an alpha-free internal format
    if (gx_format != GX_TF_CMPR) {
        // Calculate the offset and address of the mipmap
        uint32_t offset = calc_mipmap_offset(level, ti.width, ti.height, gx_format);
        dst_addr += offset;

        int dstpitch = _ogx_pitch_for_width(gx_format, ti.width);
        _ogx_bytes_to_texture(data, format, type, width, height,
                              dst_addr, gx_format, 0, 0, dstpitch);
        /* GX_TF_A8 is not supported by Dolphin and it's not properly handed by
         * a real Wii either. */
        if (gx_format == GX_TF_A8) gx_format = GX_TF_I8;
    } else {
        // Compressed texture

        // Calculate the offset and address of the mipmap
        uint32_t offset = calc_mipmap_offset(level, ti.width, ti.height, gx_format);
        dst_addr += offset;

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

        _ogx_convert_rgb_image_to_DXT1((unsigned char *)data, dst_addr,
                                       width, height, needswap);
    }

    DCFlushRange(dst_addr, calc_memory(width, height, gx_format));

    // Slow but necessary! The new textures may be in the same region of some old cached textures
    GX_InvalidateTexAll();

    GX_InitTexObj(&currtex->texobj, ti.texels,
                  ti.width, ti.height, gx_format, ti.wraps, ti.wrapt, GX_TRUE);
    GX_InitTexObjLOD(&currtex->texobj, GX_LIN_MIP_LIN, GX_LIN_MIP_LIN,
                     ti.minlevel, ti.maxlevel, 0, GX_ENABLE, GX_ENABLE, GX_ANISO_1);
    TEXTURE_RESERVE(*currtex);
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (texture < 0 || texture >= _MAX_GL_TEX)
        return;

    HANDLE_CALL_LIST(BIND_TEXTURE, target, texture);

    // If the texture has been initialized (data!=0) then load it to GX reg 0
    if (TEXTURE_IS_RESERVED(texture_list[texture])) {
        glparamstate.glcurtex = texture;

        if (TEXTURE_IS_USED(texture_list[texture]))
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
            void *data = GX_GetTexObjData(&texture_list[i].texobj);
            if (data != 0)
                free(MEM_PHYSICAL_TO_K0(data));
            memset(&texture_list[i], 0, sizeof(texture_list[i]));
        }
    }
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint *texlist = textures;
    int i;
    for (i = 0; i < _MAX_GL_TEX && n > 0; i++) {
        if (!TEXTURE_IS_RESERVED(texture_list[i])) {
            GXTexObj *texobj = &texture_list[i].texobj;
            GX_InitTexObj(texobj, NULL, 0, 0, 0, GX_REPEAT, GX_REPEAT, 0);
            TEXTURE_RESERVE(texture_list[i]);
            *texlist++ = i;
            n--;
        }
    }

    if (n > 0) {
        warning("Could not allocate %d textures", n);
        set_error(GL_OUT_OF_MEMORY);
    }
}
