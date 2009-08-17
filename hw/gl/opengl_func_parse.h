#ifndef OPENGL_FUNC_PARSE_H__
#define OPENGL_FUNC_PARSE_H__

#include "opengl_func.h"

#define CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS \
case TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_ARRAY_FLOAT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS

#define CASE_OUT_LENGTH_DEPENDING_ON_PREVIOUS_ARGS \
case TYPE_OUT_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_OUT_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_OUT_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_OUT_ARRAY_FLOAT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS: \
case TYPE_OUT_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS

#define CASE_IN_UNKNOWN_SIZE_POINTERS \
case TYPE_ARRAY_CHAR: \
case TYPE_ARRAY_SHORT: \
case TYPE_ARRAY_INT: \
case TYPE_ARRAY_FLOAT: \
case TYPE_ARRAY_DOUBLE

#define CASE_IN_KNOWN_SIZE_POINTERS \
case TYPE_1CHAR:\
case TYPE_2CHAR:\
case TYPE_3CHAR:\
case TYPE_4CHAR:\
case TYPE_128UCHAR:\
case TYPE_1SHORT:\
case TYPE_2SHORT:\
case TYPE_3SHORT:\
case TYPE_4SHORT:\
case TYPE_1INT:\
case TYPE_2INT:\
case TYPE_3INT:\
case TYPE_4INT:\
case TYPE_1FLOAT:\
case TYPE_2FLOAT:\
case TYPE_3FLOAT:\
case TYPE_4FLOAT:\
case TYPE_16FLOAT:\
case TYPE_1DOUBLE:\
case TYPE_2DOUBLE:\
case TYPE_3DOUBLE:\
case TYPE_4DOUBLE:\
case TYPE_16DOUBLE

#define CASE_OUT_UNKNOWN_SIZE_POINTERS \
case TYPE_OUT_ARRAY_CHAR: \
case TYPE_OUT_ARRAY_SHORT: \
case TYPE_OUT_ARRAY_INT: \
case TYPE_OUT_ARRAY_FLOAT: \
case TYPE_OUT_ARRAY_DOUBLE

#define CASE_OUT_KNOWN_SIZE_POINTERS \
case TYPE_OUT_1INT: \
case TYPE_OUT_1FLOAT: \
case TYPE_OUT_4CHAR: \
case TYPE_OUT_4INT: \
case TYPE_OUT_4FLOAT: \
case TYPE_OUT_4DOUBLE: \
case TYPE_OUT_128UCHAR \

#define CASE_IN_POINTERS \
CASE_IN_UNKNOWN_SIZE_POINTERS: \
CASE_IN_KNOWN_SIZE_POINTERS: \
CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS

#define CASE_OUT_POINTERS \
CASE_OUT_UNKNOWN_SIZE_POINTERS: \
CASE_OUT_KNOWN_SIZE_POINTERS: \
CASE_OUT_LENGTH_DEPENDING_ON_PREVIOUS_ARGS

#define CASE_POINTERS \
CASE_IN_POINTERS: \
CASE_OUT_POINTERS

#define CASE_KNOWN_SIZE_POINTERS \
CASE_IN_KNOWN_SIZE_POINTERS: \
CASE_OUT_KNOWN_SIZE_POINTERS

#define IS_ARRAY_CHAR(type) \
(type == TYPE_ARRAY_CHAR || \
 type == TYPE_1CHAR || \
 type == TYPE_2CHAR || \
 type == TYPE_3CHAR || \
 type == TYPE_4CHAR || \
 type == TYPE_ARRAY_CHAR_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS)

#define IS_ARRAY_SHORT(type) \
(type == TYPE_ARRAY_SHORT || \
 type == TYPE_1SHORT || \
 type == TYPE_2SHORT || \
 type == TYPE_3SHORT || \
 type == TYPE_4SHORT || \
 type == TYPE_ARRAY_SHORT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS)

#define IS_ARRAY_INT(type) \
(type == TYPE_ARRAY_INT || \
 type == TYPE_1INT || \
 type == TYPE_2INT || \
 type == TYPE_3INT || \
 type == TYPE_4INT || \
 type == TYPE_ARRAY_INT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS)

#define IS_ARRAY_FLOAT(type) \
(type == TYPE_ARRAY_FLOAT || \
 type == TYPE_1FLOAT || \
 type == TYPE_2FLOAT || \
 type == TYPE_3FLOAT || \
 type == TYPE_4FLOAT || \
 type == TYPE_16FLOAT || \
 type == TYPE_ARRAY_FLOAT_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS)

#define IS_ARRAY_DOUBLE(type) \
(type == TYPE_ARRAY_DOUBLE || \
 type == TYPE_1DOUBLE || \
 type == TYPE_2DOUBLE || \
 type == TYPE_3DOUBLE || \
 type == TYPE_4DOUBLE || \
 type == TYPE_16DOUBLE || \
 type == TYPE_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS)

static int tab_args_type_length[] = {
    0,                      /* TYPE_NONE */
    sizeof(char),           /* TYPE_CHAR */
    sizeof(unsigned char),  /* TYPE_UNSIGNED_CHAR */
    sizeof(short),          /* TYPE_SHORT */
    sizeof(unsigned short), /* TYPE_UNSIGNED_SHORT */
    sizeof(int),            /* TYPE_INT */
    sizeof(unsigned int),   /* TYPE_UNSIGNED_INT */
    sizeof(float),          /* TYPE_FLOAT */
    sizeof(double),         /* TYPE_DOUBLE */
    1 * sizeof(char),       /* TYPE_1CHAR */
    2 * sizeof(char),       /* TYPE_2CHAR */
    3 * sizeof(char),       /* TYPE_3CHAR */
    4 * sizeof(char),       /* TYPE_4CHAR */
    128 * sizeof(char),     /* TYPE_128UCHAR */
    1 * sizeof(short),      /* TYPE_1SHORT */
    2 * sizeof(short),      /* TYPE_2SHORT */
    3 * sizeof(short),      /* TYPE_3SHORT */
    4 * sizeof(short),      /* TYPE_4SHORT */
    1 * sizeof(int),        /* TYPE_1INT */
    2 * sizeof(int),        /* TYPE_2INT */
    3 * sizeof(int),        /* TYPE_3INT */
    4 * sizeof(int),        /* TYPE_4INT */
    1 * sizeof(float),      /* TYPE_1FLOAT */
    2 * sizeof(float),      /* TYPE_2FLOAT */
    3 * sizeof(float),      /* TYPE_3FLOAT */
    4 * sizeof(float),      /* TYPE_4FLOAT */
    16 * sizeof(float),     /* TYPE_16FLOAT */
    1 * sizeof(double),     /* TYPE_1DOUBLE */
    2 * sizeof(double),     /* TYPE_2DOUBLE */
    3 * sizeof(double),     /* TYPE_3DOUBLE */
    4 * sizeof(double),     /* TYPE_4DOUBLE */
    16 * sizeof(double),    /* TYPE_16DOUBLE */
    sizeof(int),            /* TYPE_OUT_1INT */
    sizeof(float),          /* TYPE_OUT_1FLOAT */
    4 * sizeof(char),       /* TYPE_OUT_4CHAR */
    4 * sizeof(int),        /* TYPE_OUT_4INT */
    4 * sizeof(float),      /* TYPE_OUT_4FLOAT */
    4 * sizeof(double),     /* TYPE_OUT_4DOUBLE */
    128 * sizeof(char),     /* TYPE_OUT_128UCHAR */
    0,                      /* TYPE_CONST_CHAR */
    0,                      /* TYPE_ARRAY_CHAR */
    0,                      /* TYPE_ARRAY_SHORT */
    0,                      /* TYPE_ARRAY_INT */
    0,                      /* TYPE_ARRAY_FLOAT */
    0,                      /* TYPE_ARRAY_DOUBLE */
    0,                      /* TYPE_IN_IGNORED_POINTER */
    0,                      /* TYPE_OUT_ARRAY_CHAR */
    0,                      /* TYPE_OUT_ARRAY_SHORT */
    0,                      /* TYPE_OUT_ARRAY_INT */
    0,                      /* TYPE_OUT_ARRAY_FLOAT */
    0,                      /* TYPE_OUT_ARRAY_DOUBLE */
    0,                      /* TYPE_NULL_TERMINATED_STRING */
    /* the following sizes are the size of 1 element of the array */
    sizeof(char),
    sizeof(short),
    sizeof(int),
    sizeof(float),
    sizeof(double),
    sizeof(char),
    sizeof(short),
    sizeof(int),
    sizeof(float),
    sizeof(double),
    /* TYPE_LAST */
};

static GLint glTexParameter_size_(FILE *err_file, GLenum pname)
{
    switch (pname) {
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_PRIORITY:
        case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_COMPARE_FAIL_VALUE_ARB:
        /*case GL_SHADOW_AMBIENT_SGIX:*/
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD:
        case GL_TEXTURE_BASE_LEVEL:
        case GL_TEXTURE_MAX_LEVEL:
        case GL_TEXTURE_CLIPMAP_FRAME_SGIX:
        case GL_TEXTURE_LOD_BIAS_S_SGIX:
        case GL_TEXTURE_LOD_BIAS_T_SGIX:
        case GL_TEXTURE_LOD_BIAS_R_SGIX:
        case GL_GENERATE_MIPMAP:
        /*case GL_GENERATE_MIPMAP_SGIS:*/
        case GL_TEXTURE_COMPARE_SGIX:
        case GL_TEXTURE_COMPARE_OPERATOR_SGIX:
        case GL_TEXTURE_MAX_CLAMP_S_SGIX:
        case GL_TEXTURE_MAX_CLAMP_T_SGIX:
        case GL_TEXTURE_MAX_CLAMP_R_SGIX:
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        case GL_TEXTURE_LOD_BIAS:
        /*case GL_TEXTURE_LOD_BIAS_EXT:*/
        case GL_DEPTH_TEXTURE_MODE:
        /*case GL_DEPTH_TEXTURE_MODE_ARB:*/
        case GL_TEXTURE_COMPARE_MODE:
        /*case GL_TEXTURE_COMPARE_MODE_ARB:*/
        case GL_TEXTURE_COMPARE_FUNC:
        /*case GL_TEXTURE_COMPARE_FUNC_ARB:*/
        case GL_TEXTURE_UNSIGNED_REMAP_MODE_NV:
            return 1;
        case GL_TEXTURE_CLIPMAP_CENTER_SGIX:
        case GL_TEXTURE_CLIPMAP_OFFSET_SGIX:
            return 2;
        case GL_TEXTURE_CLIPMAP_VIRTUAL_DEPTH_SGIX:
            return 3;
        case GL_TEXTURE_BORDER_COLOR:
        case GL_POST_TEXTURE_FILTER_BIAS_SGIX:
        case GL_POST_TEXTURE_FILTER_SCALE_SGIX:
            return 4;
        default:
            break;
    }
    fprintf(err_file, "%s: unhandled pname = 0x%x\n", __FUNCTION__, pname);
    return 0;
}


static int glLight_size_(FILE *err_file, GLenum pname)
{
    switch (pname) {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
            return 4;
        case GL_SPOT_DIRECTION:
            return 3;
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            return 1;
        default:
            break;
    }
    fprintf(err_file, "%s: unhandled pname = 0x%x\n", __FUNCTION__, pname);
    return 0;
}

static int glMaterial_size_(FILE *err_file, GLenum pname)
{
    switch (pname) {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_EMISSION:
        case GL_AMBIENT_AND_DIFFUSE:
            return 4;
        case GL_SHININESS:
            return 1;
        case GL_COLOR_INDEXES:
            return 3;
        default:
            break;
    }
    fprintf(err_file, "%s: unhandled pname = 0x%x\n", __FUNCTION__, pname);
    return 0;
}


static int compute_arg_length(FILE *err_file, int func_number, int arg_i,
                              target_ulong *args)
{
    Signature *signature = (Signature *)tab_opengl_calls[func_number];
    int *args_type = signature->args_type;
    
    switch (func_number) {
        case glProgramNamedParameter4fNV_func:
        case glProgramNamedParameter4dNV_func:
        case glProgramNamedParameter4fvNV_func:
        case glProgramNamedParameter4dvNV_func:
        case glGetProgramNamedParameterfvNV_func:
        case glGetProgramNamedParameterdvNV_func:
            if (arg_i == 2) {
                return 1 * args[arg_i - 1]
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glProgramStringARB_func:
        case glLoadProgramNV_func:
        case glGenProgramsNV_func:
        case glDeleteProgramsNV_func:
        case glGenProgramsARB_func:
        case glDeleteProgramsARB_func:
        case glRequestResidentProgramsNV_func:
        case glDrawBuffers_func:
        case glDrawBuffersARB_func:
        case glDrawBuffersATI_func:
        case glDeleteBuffers_func:
        case glDeleteBuffersARB_func:
        case glDeleteTextures_func:
        case glDeleteTexturesEXT_func:
        case glGenFramebuffersEXT_func:
        case glDeleteFramebuffersEXT_func:
        case glGenRenderbuffersEXT_func:
        case glDeleteRenderbuffersEXT_func:
        case glGenQueries_func:
        case glGenQueriesARB_func:
        case glDeleteQueries_func:
        case glDeleteQueriesARB_func:
        case glGenOcclusionQueriesNV_func:
        case glDeleteOcclusionQueriesNV_func:
        case glGenFencesNV_func:
        case glDeleteFencesNV_func:
        case glUniform1fv_func:
        case glUniform1iv_func:
        case glUniform1fvARB_func:
        case glUniform1ivARB_func:
        case glUniform1uivEXT_func:
        case glVertexAttribs1dvNV_func:
        case glVertexAttribs1fvNV_func:
        case glVertexAttribs1svNV_func:
        case glVertexAttribs1hvNV_func:
        case glWeightbvARB_func:
        case glWeightsvARB_func:
        case glWeightivARB_func:
        case glWeightfvARB_func:
        case glWeightdvARB_func:
        case glWeightubvARB_func:
        case glWeightusvARB_func:
        case glWeightuivARB_func:
        case glPixelMapfv_func:
        case glPixelMapuiv_func:
        case glPixelMapusv_func:
        case glProgramBufferParametersfvNV_func:
        case glProgramBufferParametersIivNV_func:
        case glProgramBufferParametersIuivNV_func:
        case glTransformFeedbackAttribsNV_func:
        case glTransformFeedbackVaryingsNV_func:
            if (arg_i == signature->nb_args - 1) {
                return 1 * args[arg_i - 1]
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniform2fv_func:
        case glUniform2iv_func:
        case glUniform2fvARB_func:
        case glUniform2ivARB_func:
        case glUniform2uivEXT_func:
        case glVertexAttribs2dvNV_func:
        case glVertexAttribs2fvNV_func:
        case glVertexAttribs2svNV_func:
        case glVertexAttribs2hvNV_func:
        case glDetailTexFuncSGIS_func:
        case glSharpenTexFuncSGIS_func:
            if (arg_i == signature->nb_args - 1) {
                return 2 * args[arg_i - 1]
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniform3fv_func:
        case glUniform3iv_func:
        case glUniform3fvARB_func:
        case glUniform3ivARB_func:
        case glUniform3uivEXT_func:
        case glVertexAttribs3dvNV_func:
        case glVertexAttribs3fvNV_func:
        case glVertexAttribs3svNV_func:
        case glVertexAttribs3hvNV_func:
            if (arg_i == signature->nb_args - 1) {
                return 3 * args[arg_i - 1]
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniform4fv_func:
        case glUniform4iv_func:
        case glUniform4fvARB_func:
        case glUniform4ivARB_func:
        case glUniform4uivEXT_func:
        case glVertexAttribs4dvNV_func:
        case glVertexAttribs4fvNV_func:
        case glVertexAttribs4svNV_func:
        case glVertexAttribs4hvNV_func:
        case glVertexAttribs4ubvNV_func:
        case glProgramParameters4fvNV_func:
        case glProgramParameters4dvNV_func:
        case glProgramLocalParameters4fvEXT_func:
        case glProgramEnvParameters4fvEXT_func:
        case glProgramLocalParametersI4ivNV_func:
        case glProgramLocalParametersI4uivNV_func:
        case glProgramEnvParametersI4ivNV_func:
        case glProgramEnvParametersI4uivNV_func:
            if (arg_i == signature->nb_args - 1) {
                return 4 * args[arg_i - 1]
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glPrioritizeTextures_func:
        case glPrioritizeTexturesEXT_func:
        case glAreProgramsResidentNV_func:
        case glAreTexturesResident_func:
        case glAreTexturesResidentEXT_func:
            if (arg_i == 1 || arg_i == 2) {
                return args[0] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glLightfv_func:
        case glLightiv_func:
        case glGetLightfv_func:
        case glGetLightiv_func:
        case glFragmentLightfvSGIX_func:
        case glFragmentLightivSGIX_func:
        case glGetFragmentLightfvSGIX_func:
        case glGetFragmentLightivSGIX_func:
            if (arg_i == signature->nb_args - 1) {
                return glLight_size_(err_file, args[arg_i - 1])
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glLightModelfv_func:
        case glLightModeliv_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1] == GL_LIGHT_MODEL_AMBIENT) ? 4 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glFragmentLightModelfvSGIX_func:
        case glFragmentLightModelivSGIX_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1]
                         == GL_FRAGMENT_LIGHT_MODEL_AMBIENT_SGIX) ? 4 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glMaterialfv_func:
        case glMaterialiv_func:
        case glGetMaterialfv_func:
        case glGetMaterialiv_func:
        case glFragmentMaterialfvSGIX_func:
        case glFragmentMaterialivSGIX_func:
        case glGetFragmentMaterialfvSGIX_func:
        case glGetFragmentMaterialivSGIX_func:
            if (arg_i == signature->nb_args - 1) {
                return glMaterial_size_(err_file, args[arg_i - 1])
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glTexParameterfv_func:
        case glTexParameteriv_func:
        case glGetTexParameterfv_func:
        case glGetTexParameteriv_func:
        case glTexParameterIivEXT_func:
        case glTexParameterIuivEXT_func:
        case glGetTexParameterIivEXT_func:
        case glGetTexParameterIuivEXT_func:
            if (arg_i == signature->nb_args - 1) {
                return glTexParameter_size_(err_file, args[arg_i - 1])
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glFogiv_func:
        case glFogfv_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1] == GL_FOG_COLOR) ? 4 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glTexGendv_func:
        case glTexGenfv_func:
        case glTexGeniv_func:
        case glGetTexGendv_func:
        case glGetTexGenfv_func:
        case glGetTexGeniv_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1] == GL_TEXTURE_GEN_MODE) ? 1 : 4)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glTexEnvfv_func:
        case glTexEnviv_func:
        case glGetTexEnvfv_func:
        case glGetTexEnviv_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1] == GL_TEXTURE_ENV_MODE) ? 1 : 4)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glConvolutionParameterfv_func:
        case glConvolutionParameteriv_func:
        case glGetConvolutionParameterfv_func:
        case glGetConvolutionParameteriv_func:
        case glConvolutionParameterfvEXT_func:
        case glConvolutionParameterivEXT_func:
        case glGetConvolutionParameterfvEXT_func:
        case glGetConvolutionParameterivEXT_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1] == GL_CONVOLUTION_BORDER_COLOR ||
                         args[arg_i - 1] == GL_CONVOLUTION_FILTER_SCALE ||
                         args[arg_i - 1] == GL_CONVOLUTION_FILTER_BIAS) ? 4 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glGetVertexAttribfvARB_func:
        case glGetVertexAttribfvNV_func:
        case glGetVertexAttribfv_func:
        case glGetVertexAttribdvARB_func:
        case glGetVertexAttribdvNV_func:
        case glGetVertexAttribdv_func:
        case glGetVertexAttribivARB_func:
        case glGetVertexAttribivNV_func:
        case glGetVertexAttribiv_func:
        case glGetVertexAttribIivEXT_func:
        case glGetVertexAttribIuivEXT_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1]
                         == GL_CURRENT_VERTEX_ATTRIB_ARB) ? 4 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glPointParameterfv_func:
        case glPointParameterfvEXT_func:
        case glPointParameterfvARB_func:
        case glPointParameterfvSGIS_func:
        case glPointParameteriv_func:
        case glPointParameterivEXT_func:
            if (arg_i == signature->nb_args - 1) {
                return ((args[arg_i - 1]
                         == GL_POINT_DISTANCE_ATTENUATION) ? 3 : 1)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix2fv_func:
        case glUniformMatrix2fvARB_func:
            if (arg_i == signature->nb_args - 1) {
                return 2 * 2 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix3fv_func:
        case glUniformMatrix3fvARB_func:
            if (arg_i == signature->nb_args - 1) {
                return 3 * 3 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix4fv_func:
        case glUniformMatrix4fvARB_func:
            if (arg_i == signature->nb_args - 1) {
                return 4 * 4 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix2x3fv_func:
        case glUniformMatrix3x2fv_func:
            if (arg_i == signature->nb_args - 1) {
                return 2 * 3 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix2x4fv_func:
        case glUniformMatrix4x2fv_func:
            if (arg_i == signature->nb_args - 1) {
                return 2 * 4 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glUniformMatrix3x4fv_func:
        case glUniformMatrix4x3fv_func:
            if (arg_i == signature->nb_args - 1) {
                return 3 * 4 * args[1] * tab_args_type_length[args_type[arg_i]];
            }
            break;
        case glSpriteParameterivSGIX_func:
        case glSpriteParameterfvSGIX_func:
            if  (arg_i == signature->nb_args - 1) {
                return ((args[arg_i-1] == GL_SPRITE_MODE_SGIX) ? 1 : 3)
                    * tab_args_type_length[args_type[arg_i]];
            }
            break;
        default:
            break;
    }
    fprintf(err_file, "invalid combination for %s: func_number=%d, arg_i=%d\n",
            __FUNCTION__, func_number, arg_i);
    return 0;
}

#define IS_NULL_POINTER_OK_FOR_FUNC(func_number) \
    (func_number == glBitmap_func || \
    func_number == glTexImage1D_func || \
    func_number == glTexImage2D_func || \
    func_number == glTexImage3D_func || \
    func_number == glTexImage3DEXT_func || \
    func_number == glBufferDataARB_func || \
    func_number == glNewObjectBufferATI_func)
#endif
