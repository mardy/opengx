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

#include "pixel_stream.h"

const struct MasksPerType _ogx_pixels_masks_per_type[] = {
    {GL_UNSIGNED_BYTE_3_3_2, 1, 3, 3, 2, 0, 0, 3, 6, 0 },
    {GL_UNSIGNED_BYTE_2_3_3_REV, 1, 3, 3, 2, 0, 5, 2, 0, 0 },
    {GL_UNSIGNED_SHORT_5_6_5, 2, 5, 6, 5, 0, 0, 5, 11, 0 },
    {GL_UNSIGNED_SHORT_5_6_5_REV, 2, 5, 6, 5, 0, 11, 5, 0, 0 },
    {GL_UNSIGNED_SHORT_4_4_4_4, 2, 4, 4, 4, 4, 0, 4, 8, 12 },
    {GL_UNSIGNED_SHORT_4_4_4_4_REV, 2, 4, 4, 4, 4, 12, 8, 4, 0 },
    {GL_UNSIGNED_SHORT_5_5_5_1, 2, 5, 5, 5, 1, 0, 5, 10, 15 },
    {GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, 5, 5, 5, 1, 11, 6, 1, 0 },
    {GL_UNSIGNED_INT_8_8_8_8, 4, 8, 8, 8, 8, 0, 8, 16, 24 },
    {GL_UNSIGNED_INT_8_8_8_8_REV, 4, 8, 8, 8, 8, 24, 16, 8, 0 },
    {GL_UNSIGNED_INT_10_10_10_2, 4, 10, 10, 10, 2, 0, 10, 20, 30 },
    {GL_UNSIGNED_INT_2_10_10_10_REV, 4, 10, 10, 10, 2, 22, 12, 2, 0 },
    {0, }
};

const struct ComponentsPerFormat _ogx_pixels_components_per_format[] = {
    { GL_RGBA, 4, { 0, 1, 2, 3 }},
    { GL_BGRA, 4, { 2, 1, 0, 3 }},
    { GL_RGB, 3, { 0, 1, 2 }},
    { GL_BGR, 3, { 2, 1, 0 }},
    { GL_LUMINANCE_ALPHA, 2, { 0, 3 }},
    { GL_INTENSITY, 1, { 0 }},
    { GL_LUMINANCE, 1, { 0 }},
    { GL_RED, 1, { 0 }},
    { GL_GREEN, 1, { 1 }},
    { GL_BLUE, 1, { 2 }},
    { GL_ALPHA, 1, { 3 }},
    { 0, }
};
