/*
 *  Main header for both host and guest sides
 * 
 *  Copyright (c) 2006,2007 Even Rouault
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef OPENGL_FUNC_H__
#define OPENGL_FUNC_H__

#include "mesa_gl.h"
#include "mesa_glext.h"

enum {
/*  0 */  TYPE_NONE,
/*  1 */  TYPE_CHAR,
/*  2 */  TYPE_UNSIGNED_CHAR,
/*  3 */  TYPE_SHORT,
/*  4 */  TYPE_UNSIGNED_SHORT,
/*  5 */  TYPE_INT,
/*  6 */  TYPE_UNSIGNED_INT,
/*  7 */  TYPE_FLOAT,
/*  8 */  TYPE_DOUBLE,
/*  9 */  TYPE_1CHAR,
/* 10 */  TYPE_2CHAR,
/* 11 */  TYPE_3CHAR,
/* 12 */  TYPE_4CHAR,
/* 13 */  TYPE_128UCHAR,
/* 14 */  TYPE_1SHORT,
/* 15 */  TYPE_2SHORT,
/* 16 */  TYPE_3SHORT,
/* 17 */  TYPE_4SHORT,
/* 18 */  TYPE_1INT,
/* 19 */  TYPE_2INT,
/* 20 */  TYPE_3INT,
/* 21 */  TYPE_4INT,
/* 22 */  TYPE_1FLOAT,
/* 23 */  TYPE_2FLOAT,
/* 24 */  TYPE_3FLOAT,
/* 25 */  TYPE_4FLOAT,
/* 26 */  TYPE_16FLOAT,
/* 27 */  TYPE_1DOUBLE,
/* 28 */  TYPE_2DOUBLE,
/* 29 */  TYPE_3DOUBLE,
/* 30 */  TYPE_4DOUBLE,
/* 31 */  TYPE_16DOUBLE,
/* 32 */  TYPE_OUT_1INT,
/* 33 */  TYPE_OUT_1FLOAT,
/* 34 */  TYPE_OUT_4CHAR,
/* 35 */  TYPE_OUT_4INT,
/* 36 */  TYPE_OUT_4FLOAT,
/* 37 */  TYPE_OUT_4DOUBLE,
/* 38 */  TYPE_OUT_128UCHAR,
/* 39 */  TYPE_CONST_CHAR,
/* 40 */  TYPE_ARRAY_CHAR,
/* 41 */  TYPE_ARRAY_SHORT,
/* 42 */  TYPE_ARRAY_INT,
/* 43 */  TYPE_ARRAY_FLOAT,
/* 44 */  TYPE_ARRAY_DOUBLE,
/* 45 */  TYPE_IN_IGNORED_POINTER,
/* 46 */  TYPE_OUT_ARRAY_CHAR,
/* 47 */  TYPE_OUT_ARRAY_SHORT,
/* 48 */  TYPE_OUT_ARRAY_INT,
/* 49 */  TYPE_OUT_ARRAY_FLOAT,
/* 50 */  TYPE_OUT_ARRAY_DOUBLE,
/* 51 */  TYPE_NULL_TERMINATED_STRING,
  
/* 52 */  TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 53 */  TYPE_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 54 */  TYPE_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 55 */  TYPE_ARRAY_FLOAT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 56 */  TYPE_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 57 */  TYPE_OUT_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 58 */  TYPE_OUT_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 59 */  TYPE_OUT_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 60 */  TYPE_OUT_ARRAY_FLOAT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
/* 61 */  TYPE_OUT_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
 /* .... */
/* 62 */  TYPE_LAST,
 /* .... */
  TYPE_1UCHAR = TYPE_CHAR,
  TYPE_1USHORT = TYPE_1SHORT,
  TYPE_1UINT = TYPE_1INT,
  TYPE_OUT_1UINT = TYPE_OUT_1INT,
  TYPE_OUT_4UCHAR = TYPE_OUT_4CHAR,
  TYPE_ARRAY_VOID = TYPE_ARRAY_CHAR,
  TYPE_ARRAY_SIGNED_CHAR = TYPE_ARRAY_CHAR,
  TYPE_ARRAY_UNSIGNED_CHAR = TYPE_ARRAY_CHAR,
  TYPE_ARRAY_UNSIGNED_SHORT = TYPE_ARRAY_SHORT,
  TYPE_ARRAY_UNSIGNED_INT = TYPE_ARRAY_INT,
  TYPE_OUT_ARRAY_VOID = TYPE_OUT_ARRAY_CHAR,
  TYPE_OUT_ARRAY_SIGNED_CHAR = TYPE_OUT_ARRAY_CHAR,
  TYPE_OUT_ARRAY_UNSIGNED_CHAR = TYPE_OUT_ARRAY_CHAR,
  TYPE_OUT_ARRAY_UNSIGNED_SHORT = TYPE_OUT_ARRAY_SHORT,
  TYPE_OUT_ARRAY_UNSIGNED_INT = TYPE_OUT_ARRAY_INT,
  TYPE_ARRAY_VOID_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_ARRAY_SIGNED_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_ARRAY_UNSIGNED_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_ARRAY_UNSIGNED_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_ARRAY_UNSIGNED_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_OUT_ARRAY_VOID_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_OUT_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_OUT_ARRAY_SIGNED_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_OUT_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_OUT_ARRAY_UNSIGNED_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_OUT_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_OUT_ARRAY_UNSIGNED_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_OUT_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
  TYPE_OUT_ARRAY_UNSIGNED_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS = TYPE_OUT_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS,
};  

#define NB_MAX_TEXTURES 16
#define MY_GL_MAX_VERTEX_ATTRIBS_ARB 16
#define MY_GL_MAX_VERTEX_ATTRIBS_NV 16
#define MY_GL_MAX_VARIANT_POINTER_EXT 16

typedef struct {
    int ret_type;
    int has_out_parameters;
    int nb_args;
    int args_type[0];
} Signature;

/* internal functions */

static const int _init_signature[] = {TYPE_INT, 0, 0};
static const int _exit_process_signature[] = {TYPE_NONE, 0, 0};
static const int _createDrawable_signature[] = {TYPE_NONE, 0, 5,
                                                TYPE_INT,  // drawable
                                                TYPE_INT,  // shmaddr
                                                TYPE_INT,  // depth
                                                TYPE_INT,  // width
                                                TYPE_INT}; // height
static const int _resizeDrawable_signature[] = {TYPE_NONE, 0, 3,
                                                TYPE_INT,  // drawable
                                                TYPE_INT,  // width
                                                TYPE_INT}; // height

/* GLX 1.0 and later */

static const int glXChooseVisual_signature[] = {TYPE_INT, 0, 2,
                                                TYPE_INT,        // depth
                                                TYPE_ARRAY_INT}; // attribs
static const int glXCreateContext_signature[] = {TYPE_INT, 0, 2,
                                                 TYPE_INT,  // vis
                                                 TYPE_INT}; // shareList
static const int glXCopyContext_signature[] = {TYPE_NONE, 0, 3, 
                                               TYPE_INT,  // src
                                               TYPE_INT,  // dst
                                               TYPE_INT}; // mask
static const int glXDestroyContext_signature[] = {TYPE_NONE, 0, 1,
                                                  TYPE_INT}; // ctx
static const int glXMakeCurrent_signature[] = {TYPE_NONE, 0, 2,
                                               TYPE_INT,  // drawable
                                               TYPE_INT}; // ctx
static const int glXGetConfig_signature[] = {TYPE_INT, 1, 3,
                                             TYPE_INT,       // visual
                                             TYPE_INT,       // attrib
                                             TYPE_OUT_1INT}; // value
static const int glXGetConfig_extended_signature[] = {TYPE_NONE, 1, 5,
                                                      TYPE_INT, // visual_id
                                                      TYPE_INT, // n
                                                      TYPE_ARRAY_INT, // attribs
                                                      TYPE_OUT_ARRAY_INT, // values
                                                      TYPE_OUT_ARRAY_INT}; // rets
static const int glXSwapBuffers_signature[] = {TYPE_NONE, 0, 1,
                                               TYPE_INT}; // drawable
static const int glXWaitGL_signature[] = { TYPE_INT, 0, 0 };
// glXQueryVersion implemented locally only
// glXQueryExtension implemented locally only

/* GLX 1.1 and later */

static const int glXGetProcAddress_fake_signature[] = {TYPE_INT, 0, 1,
                                                       TYPE_NULL_TERMINATED_STRING};
static const int glXGetProcAddress_global_fake_signature[] = {TYPE_NONE, 1, 3,
                                                              TYPE_INT,
                                                              TYPE_ARRAY_CHAR,
                                                              TYPE_OUT_ARRAY_CHAR};
// glXGetClientString implemented locally only
// glXQueryExtensionsString implemented locally only
// glXQueryServerString implemented locally only

/* GLX 1.3 and later */

static const int glXDestroyPbuffer_signature[] = {TYPE_NONE, 0, 1,
                                                  TYPE_INT}; // pbuffer
static const int glXSwapIntervalSGI_signature[] = {TYPE_INT, 0, 1,
                                                   TYPE_INT}; // interval
// glXIsDirect implemented locally only
/* the following are currently not implemented --->
static const int glXChooseFBConfig_signature[] = {TYPE_INT, 1, 2,
                                                  TYPE_ARRAY_INT, // attribList
                                                  TYPE_OUT_1INT}; // nitems
static const int glXChooseFBConfigSGIX_signature[] = {TYPE_INT, 1, 2,
                                                      TYPE_ARRAY_INT, // attribList
                                                      TYPE_OUT_1INT}; // nitems
static const int glXGetFBConfigs_signature[] = {TYPE_INT, 1, 1,
                                                TYPE_OUT_1INT}; // nelements
static const int glXGetFBConfigAttrib_extended_signature[] = {TYPE_NONE, 1, 5,
                                                              TYPE_INT, // fbconfig
                                                              TYPE_INT, // n
                                                              TYPE_ARRAY_INT, // attribs
                                                              TYPE_OUT_ARRAY_INT, // values
                                                              TYPE_OUT_ARRAY_INT}; // rets
static const int glXCreatePbuffer_signature[] = {TYPE_INT, 0, 2,
                                                 TYPE_INT, // config
                                                 TYPE_ARRAY_INT}; // attribList
static const int glXCreateGLXPbufferSGIX_signature[] = {TYPE_INT, 0, 4,
                                                        TYPE_INT, // config
                                                        TYPE_INT, // width
                                                        TYPE_INT, // height
                                                        TYPE_ARRAY_INT}; // attribs
static const int glXDestroyGLXPbufferSGIX_signature[] = {TYPE_NONE, 0, 1,
                                                         TYPE_INT}; // pbuffer
static const int glXCreateNewContext_signature[] = {TYPE_INT, 0, 4,
                                                    TYPE_INT,  // config
                                                    TYPE_INT,  // renderType
                                                    TYPE_INT,  // shareList
                                                    TYPE_INT}; // direct
static const int glXCreateContextWithConfigSGIX_signature[] = {TYPE_INT, 0, 4,
                                                               TYPE_INT,  // config
                                                               TYPE_INT,  // renderType
                                                               TYPE_INT,  // shareList
                                                               TYPE_INT}; // direct
static const int glXGetVisualFromFBConfig_signature[] = {TYPE_INT, 0, 1,
                                                         TYPE_INT}; // config
static const int glXGetFBConfigAttrib_signature[] = {TYPE_INT, 1, 3, 
                                                     TYPE_INT,       // config
                                                     TYPE_INT,       // attrib
                                                     TYPE_OUT_1INT}; // value
static const int glXGetFBConfigAttribSGIX_signature[] = {TYPE_INT, 1, 3,
                                                         TYPE_INT,       // config
                                                         TYPE_INT,       // attrib
                                                         TYPE_OUT_1INT}; // value
static const int glXQueryContext_signature[] = {TYPE_INT, 1, 3,
                                                TYPE_INT,       // context
                                                TYPE_INT,       // attribute
                                                TYPE_OUT_1INT}; // value
static const int glXQueryGLXPbufferSGIX_signature[] = {TYPE_INT, 1, 3,
                                                       TYPE_INT,       // context
                                                       TYPE_INT,       // attrib
                                                       TYPE_OUT_1INT}; // value
static const int glXQueryDrawable_signature[] = {TYPE_NONE, 1, 3,
                                                 TYPE_INT,       // drawable
                                                 TYPE_INT,       // attrib
                                                 TYPE_OUT_1INT}; // value
static const int glXUseXFont_signature[] = {TYPE_NONE, 0, 4,
                                            TYPE_INT,  // font
                                            TYPE_INT,  // first
                                            TYPE_INT,  // count
                                            TYPE_INT}; // list
static const int glXGetScreenDriver_signature[] = {TYPE_CONST_CHAR, 0, 2, TYPE_IN_IGNORED_POINTER, TYPE_INT };
static const int glXGetDriverConfig_signature[] = { TYPE_CONST_CHAR, 0, 1, TYPE_NULL_TERMINATED_STRING };
static const int glXWaitVideoSyncSGI_signature[] = { TYPE_INT, 1, 3, TYPE_INT, TYPE_INT, TYPE_OUT_1INT };
static const int glXGetVideoSyncSGI_signature[] = { TYPE_INT, 1, 1, TYPE_OUT_1INT };
static const int glXBindTexImageATI_signature[] = { TYPE_NONE, 0, 3, TYPE_IN_IGNORED_POINTER, TYPE_INT, TYPE_INT };
static const int glXReleaseTexImageATI_signature[] = { TYPE_NONE, 0, 3, TYPE_IN_IGNORED_POINTER, TYPE_INT, TYPE_INT };
static const int glXBindTexImageARB_signature[] = { TYPE_INT, 0, 3, TYPE_IN_IGNORED_POINTER, TYPE_INT, TYPE_INT };
static const int glXReleaseTexImageARB_signature[] = { TYPE_INT, 0, 3, TYPE_IN_IGNORED_POINTER, TYPE_INT, TYPE_INT };
*/

static const int glGetString_signature[] = {TYPE_CONST_CHAR, 0, 1, TYPE_INT};
static const int glShaderSourceARB_fake_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR, TYPE_ARRAY_INT};
static const int glShaderSource_fake_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR, TYPE_ARRAY_INT};

static const int glVertexPointer_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glNormalPointer_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glColorPointer_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glSecondaryColorPointer_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glIndexPointer_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glTexCoordPointer_fake_signature[] = { TYPE_NONE, 0, 7, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glEdgeFlagPointer_fake_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexAttribPointerARB_fake_signature[] = { TYPE_NONE, 0, 8, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexAttribPointerNV_fake_signature[] = { TYPE_NONE, 0, 7, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glWeightPointerARB_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glMatrixIndexPointerARB_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glFogCoordPointer_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glInterleavedArrays_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glElementPointerATI_fake_signature[] = { TYPE_NONE, 0, 3, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR }; 
static const int glVariantPointerEXT_fake_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR }; 
static const int glTuxRacerDrawElements_fake_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexAndNormalPointer_fake_signature[] = { TYPE_NONE, 0, 7, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glTexCoordPointer01_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glTexCoordPointer012_fake_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalPointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 8, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalColorPointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 11, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexColorTexCoord0PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 12, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalTexCoord0PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 11, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalTexCoord01PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 14, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT,  TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalTexCoord012PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 17, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT,  TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalColorTexCoord0PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 14, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT,  TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalColorTexCoord01PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 17, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT,  TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };
static const int glVertexNormalColorTexCoord012PointerInterlaced_fake_signature[] = {TYPE_NONE, 0, 20, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT,  TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_ARRAY_CHAR };

static const int glGenTextures_fake_signature[] = { TYPE_NONE, 0, 1, TYPE_INT};
static const int glGenBuffersARB_fake_signature[] = { TYPE_NONE, 0, 1, TYPE_INT};
static const int glGenLists_fake_signature[] = { TYPE_NONE, 0, 1, TYPE_INT};

static const int _glDrawElements_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glDrawRangeElements_buffer_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glMultiDrawElements_buffer_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_ARRAY_INT, TYPE_INT, TYPE_ARRAY_INT, TYPE_INT };

static const int _glVertexPointer_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glNormalPointer_buffer_signature[] = { TYPE_NONE, 0, 3, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glColorPointer_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glSecondaryColorPointer_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glIndexPointer_buffer_signature[] = { TYPE_NONE, 0, 3, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glTexCoordPointer_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glEdgeFlagPointer_buffer_signature[] = { TYPE_NONE, 0, 2, TYPE_INT, TYPE_INT};
static const int _glVertexAttribPointerARB_buffer_signature[] = { TYPE_NONE, 0, 6, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glWeightPointerARB_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glMatrixIndexPointerARB_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glFogCoordPointer_buffer_signature[] = { TYPE_NONE, 0, 3, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glVariantPointerEXT_buffer_signature[] = { TYPE_NONE, 0, 4, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};

static const int _glReadPixels_pbo_signature[] = { TYPE_INT, 0, 7, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glDrawPixels_pbo_signature[] = { TYPE_NONE, 0, 5, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT};
static const int _glMapBufferARB_fake_signature[] = { TYPE_INT, 1, 3, TYPE_INT, TYPE_INT, TYPE_OUT_ARRAY_CHAR };

static const int _glSelectBuffer_fake_signature[] = { TYPE_NONE, 0, 1, TYPE_INT };
static const int _glGetSelectBuffer_fake_signature[] = { TYPE_NONE, 1, 1, TYPE_ARRAY_CHAR };
static const int _glFeedbackBuffer_fake_signature[] = { TYPE_NONE, 0, 2, TYPE_INT, TYPE_INT };
static const int _glGetFeedbackBuffer_fake_signature[] = { TYPE_NONE, 1, 1, TYPE_ARRAY_CHAR };

static const int _glGetError_fake_signature[] = { TYPE_NONE, 0, 0 };
    
#include "gl_func.h"

#endif
