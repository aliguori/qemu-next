/*
 *  Parse gl.h et glx.h to auto-generate source code
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

/* gcc -g parse_gl_h.c -o parse_gl_h && ./parse_gl_h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int isExtByName(const char* name)
{
    return (strstr(name, "ARB") != NULL) ||
           (strstr(name, "IBM") != NULL) ||
           (strstr(name, "EXT") != NULL) ||
           (strstr(name, "ATI") != NULL) ||
           (strstr(name, "NV") != NULL) ||
           (strstr(name, "MESA") != NULL) ||
           (strstr(name, "APPLE") != NULL) ||
           (strstr(name, "SUN") != NULL) ||
           (strstr(name, "SGI") != NULL);
}

char* get_arg_type(char* s)
{
    while(*s == ' ' || *s == '\t') s++;
    char* n = s;
    char* c = strstr(n, "const");
    if (c)
        n += 6;
    
    char* t = strstr(n, " ");
    if (t) {
        if (t[1] == '*')
            t += 2;
        t[0] = 0;
        char* ori = t;
        t = strstr(t+1, "[");
        if (t) {
            memmove(ori, t, strlen(t));
            strstr(ori, "]")[1] = 0;
        }
    }
    return strdup(s);
}

typedef struct {
    char* type;
    char* name;
    int nargs;
    char** args;
    int ok;
    int just_for_server_side;
    int has_out_parameters;
    int isExt;
} FuncDesc;

int isExt(FuncDesc* func)
{
    return func->isExt;
}

char* get_type_string(char* type)
{
    if (strstr(type, "[16]")) {
        if (strstr(type, "float"))
            return ("TYPE_16FLOAT");
        else if (strstr(type, "double"))
            return ("TYPE_16DOUBLE");
        else {
            printf("inconnu %s\n", type);
            exit(-1);
        }
    } else if (strstr(type, "[128]") && strstr(type, "GLubyte"))
        return strstr(type, "const") ? "TYPE_128UCHAR" : "TYPE_OUT_128UCHAR";
    else if (strstr(type, "const GLvoid *"))
        return "TYPE_ARRAY_VOID";
    else if (strstr(type, "const GLchar *") ||
             strstr(type, "const GLcharARB *"))
        return "TYPE_NULL_TERMINATED_STRING";
    else if (strstr(type, "const GLbyte *"))
        return "TYPE_ARRAY_SIGNED_CHAR";
    else if (strstr(type, "const GLubyte *"))
        return "TYPE_ARRAY_UNSIGNED_CHAR";
    else if (strstr(type, "const GLshort *"))
        return "TYPE_ARRAY_SHORT";
    else if (strstr(type, "const GLushort *") ||
             strstr(type, "const GLhalfNV *"))
        return "TYPE_ARRAY_UNSIGNED_SHORT";
    else if (strstr(type, "const GLint *"))
        return "TYPE_ARRAY_INT";
    else if (strstr(type, "const GLuint *") ||
             strstr(type, "const GLenum *"))
        return "TYPE_ARRAY_UNSIGNED_INT";
    else if (strstr(type, "const GLfloat *") ||
             strstr(type, "const GLclampf *"))
        return "TYPE_ARRAY_FLOAT";
    else if (strstr(type, "const GLdouble *"))
        return "TYPE_ARRAY_DOUBLE";
    else if (strstr(type, "GLvoid *"))
        return "TYPE_OUT_ARRAY_VOID";
    else if (strstr(type, "GLboolean *") ||
             strstr(type, "GLubyte *"))
        return "TYPE_OUT_ARRAY_UNSIGNED_CHAR";
    else if (strstr(type, "GLcharARB *") ||
             strstr(type, "GLchar *"))
        return "TYPE_OUT_ARRAY_CHAR";
    else if (strstr(type, "GLshort *"))
        return "TYPE_OUT_ARRAY_SHORT";
    else if (strstr(type, "GLushort *"))
        return "TYPE_OUT_ARRAY_UNSIGNED_SHORT";
    else if (strstr(type, "GLint *")||
             strstr(type, "GLsizei *"))
        return "TYPE_OUT_ARRAY_INT";
    else if (strstr(type, "GLuint *") ||
             strstr(type, "GLenum *") ||
             strstr(type, "GLhandleARB *"))
        return "TYPE_OUT_ARRAY_UNSIGNED_INT";
    else if (strstr(type, "GLfloat *"))
        return "TYPE_OUT_ARRAY_FLOAT";
    else if (strstr(type, "GLdouble *"))
        return "TYPE_OUT_ARRAY_DOUBLE";
    else if (strcmp(type, "void") == 0)
        return("TYPE_NONE");
    else if (strcmp(type, "GLbyte") == 0)
        return("TYPE_CHAR");
    else if (strcmp(type, "GLubyte") == 0 ||
             strcmp(type, "GLboolean") == 0)
        return("TYPE_UNSIGNED_CHAR");
    else if (strcmp(type, "GLshort") == 0)
        return("TYPE_SHORT");
    else if (strcmp(type, "GLushort") == 0 ||
             strcmp(type, "GLhalfNV") == 0)
        return("TYPE_UNSIGNED_SHORT");
    else if (strcmp(type, "GLint") == 0 ||
             strcmp(type, "GLsizei") == 0 ||
             strcmp(type, "GLintptr") == 0 ||
             strcmp(type, "GLsizeiptr") == 0 ||
             strcmp(type, "GLintptrARB") == 0 ||
             strcmp(type, "GLsizeiptrARB") == 0)
        return("TYPE_INT");
    else if (strcmp(type, "GLenum") == 0 ||
             strcmp(type, "GLuint") == 0 ||
             strcmp(type, "GLhandleARB") == 0 ||
             strcmp(type, "GLbitfield") == 0)
        return("TYPE_UNSIGNED_INT");
    else if (strcmp(type, "GLfloat") == 0 ||
             strcmp(type, "GLclampf") == 0)
        return("TYPE_FLOAT");
    else if (strcmp(type, "GLdouble") == 0 ||
             strcmp(type, "GLclampd") == 0)
        return("TYPE_DOUBLE");
    else {
        printf("inconnu %s\n", type);
        exit(-1);
    }
}

typedef struct {
    char* letter;
    char* signature_type_name;
    char* gl_c_type_name;
    char* c_type_name;
} ForIsKnownArgVector;

#define N_ELEMENTS(x)  (sizeof(x)/sizeof(x[0]))
#define N_FIELDS_IN_ARG_VECTOR  4

typedef struct {
    char* func_name;
    char* signature_type_name;
} KnownLastArgFunc;

static KnownLastArgFunc knownLastArgFuncs[] = {
    {"glFogCoordfv", "TYPE_1FLOAT"},
    {"glFogCoorddv", "TYPE_1DOUBLE"},
    {"glFogCoordfvEXT", "TYPE_1FLOAT"},
    {"glFogCoorddvEXT", "TYPE_1DOUBLE"},
    {"glFogCoordhvNV", "TYPE_1USHORT"},

    {"glGetFenceivNV", "TYPE_OUT_1INT"},

    {"glGetTexLevelParameteriv", "TYPE_OUT_1INT" },
    {"glGetTexLevelParameterfv", "TYPE_OUT_1FLOAT" },

    {"glGetRenderbufferParameterivEXT", "TYPE_OUT_1INT"},
    {"glGetFramebufferAttachmentParameterivEXT", "TYPE_OUT_1INT"},
    {"glGetFinalCombinerInputParameterivNV", "TYPE_OUT_1INT"},
    {"glGetCombinerOutputParameterivNV", "TYPE_OUT_1INT"},
    {"glGetCombinerInputParameterivNV", "TYPE_OUT_1INT"},
    {"glGetOcclusionQueryivNV", "TYPE_OUT_1INT"},
    {"glGetOcclusionQueryuivNV", "TYPE_OUT_1UINT"},
    {"glGetObjectParameterivARB", "TYPE_OUT_1INT"},
    {"glGetQueryivARB", "TYPE_OUT_1INT"},
    {"glGetQueryiv", "TYPE_OUT_1INT"},
    {"glGetQueryObjectivARB", "TYPE_OUT_1INT"},
    {"glGetQueryObjectiv", "TYPE_OUT_1INT"},
    {"glGetQueryObjectuivARB", "TYPE_OUT_1UINT"},
    {"glGetQueryObjectuiv", "TYPE_OUT_1UINT"},
    {"glGetProgramivARB", "TYPE_OUT_1INT"},
    {"glGetProgramiv", "TYPE_OUT_1INT"},
    {"glGetProgramivNV", "TYPE_OUT_1INT"},
    {"glGetShaderiv", "TYPE_OUT_1INT"},

    {"glCombinerParameterfvNV", "TYPE_1FLOAT"},
    {"glCombinerParameterivNV", "TYPE_1INT"},

    {"glGetFinalCombinerInputParameterfvNV", "TYPE_OUT_1FLOAT"},
    {"glGetCombinerOutputParameterfvNV", "TYPE_OUT_1FLOAT"},
    {"glGetCombinerInputParameterfvNV", "TYPE_OUT_1FLOAT"},
    {"glGetObjectParameterfvARB", "TYPE_OUT_1FLOAT"},

    {"glCombinerStageParameterfvNV", "TYPE_4FLOAT"},
    {"glGetCombinerStageParameterfvNV", "TYPE_OUT_4FLOAT"},

    {"glTexBumpParameterivATI", "TYPE_1INT"},
    {"glTexBumpParameterfvATI", "TYPE_1FLOAT"},
    {"glGetTexBumpParameterivATI", "TYPE_OUT_1INT"},
    {"glGetTexBumpParameterfvATI", "TYPE_OUT_1FLOAT"},

    {"glGetProgramLocalParameterfvARB", "TYPE_OUT_4FLOAT"},
    {"glGetProgramLocalParameterdvARB", "TYPE_OUT_4DOUBLE"},
    {"glGetProgramEnvParameterfvARB", "TYPE_OUT_4FLOAT"},
    {"glGetProgramEnvParameterdvARB", "TYPE_OUT_4DOUBLE"},
    {"glGetProgramLocalParameterIivNV", "TYPE_OUT_1INT"},
    {"glGetProgramLocalParameterIuivNV", "TYPE_OUT_1UINT"},
    {"glGetProgramEnvParameterIivNV", "TYPE_OUT_1INT"},
    {"glGetProgramEnvParameterIuivNV", "TYPE_OUT_1UINT"},

    {"glGetProgramParameterfvNV", "TYPE_OUT_4FLOAT"},
    {"glGetProgramParameterdvNV", "TYPE_OUT_4DOUBLE"},
    {"glGetProgramNamedParameterfvNV", "TYPE_OUT_4FLOAT"},
    {"glGetProgramNamedParameterdvNV", "TYPE_OUT_4DOUBLE"},

    {"glCullParameterfvEXT", "TYPE_4FLOAT"},
    {"glCullParameterdvEXT", "TYPE_4DOUBLE"},

    {"glGetTrackMatrixivNV", "TYPE_OUT_1INT"},
    {"glExecuteProgramNV", "TYPE_4FLOAT"},

    {"glEdgeFlagv", "TYPE_1UCHAR"},

    {"glClipPlane", "TYPE_4DOUBLE"},
    {"glGetClipPlane", "TYPE_OUT_4DOUBLE"},

    {"glSetFragmentShaderConstantATI", "TYPE_4FLOAT"},

    {"glGetObjectBufferfvATI", "TYPE_OUT_1FLOAT"},
    {"glGetObjectBufferivATI", "TYPE_OUT_1INT"},
    {"glGetArrayObjectfvATI", "TYPE_OUT_1FLOAT"},
    {"glGetArrayObjectivATI", "TYPE_OUT_1INT"},
    {"glGetVariantArrayObjectfvATI", "TYPE_OUT_1FLOAT"},
    {"glGetVariantArrayObjectivATI", "TYPE_OUT_1INT"},
    {"glGetVertexAttribArrayObjectfvATI", "TYPE_OUT_1FLOAT"},
    {"glGetVertexAttribArrayObjectivATI", "TYPE_OUT_1INT"},

    {"glPixelTransformParameterivEXT", "TYPE_1INT"},
    {"glPixelTransformParameterfvEXT", "TYPE_1FLOAT"},
    {"glGetPixelTransformParameterivEXT", "TYPE_OUT_1INT"},
    {"glGetPixelTransformParameterfvEXT", "TYPE_OUT_1FLOAT"},

    {"glColorTableParameterfv", "TYPE_4FLOAT"},
    {"glColorTableParameteriv", "TYPE_4INT"},
    {"glGetColorTableParameterfv", "TYPE_OUT_4FLOAT"},
    {"glGetColorTableParameteriv", "TYPE_OUT_4INT"},
    {"glColorTableParameterfvEXT", "TYPE_4FLOAT"},
    {"glColorTableParameterivEXT", "TYPE_4INT"},
    {"glGetColorTableParameterfvEXT", "TYPE_OUT_4FLOAT"},
    {"glGetColorTableParameterivEXT", "TYPE_OUT_4INT"},

    {"glGetMinmaxParameterfv", "TYPE_OUT_1FLOAT"},
    {"glGetMinmaxParameteriv", "TYPE_OUT_1INT"},
    {"glGetHistogramParameterfv", "TYPE_OUT_1FLOAT"},
    {"glGetHistogramParameteriv", "TYPE_OUT_1INT"},
    {"glGetMinmaxParameterfvEXT", "TYPE_OUT_1FLOAT"},
    {"glGetMinmaxParameterivEXT", "TYPE_OUT_1INT"},
    {"glGetHistogramParameterfvEXT", "TYPE_OUT_1FLOAT"},
    {"glGetHistogramParameterivEXT", "TYPE_OUT_1INT"},

    /* Not sure at all for the 2 followingo ones ! */
    {"glGetBooleanIndexedvEXT", "TYPE_OUT_4UCHAR"},
    {"glGetIntegerIndexedvEXT", "TYPE_OUT_4INT"},

    {"glReferencePlaneSGIX", "TYPE_4DOUBLE"},

    {"glGetTransformFeedbackVaryingNV", "TYPE_OUT_1INT"},

    {"glGetBufferParameteriv",    "TYPE_OUT_1INT"},
    {"glGetBufferParameterivARB", "TYPE_OUT_1INT"},
};

int is_known_arg_vector(FuncDesc* desc, char** p_signature_type_name, char** p_c_type_name)
{
    static ForIsKnownArgVector my_tab[] = {
        { "b", "CHAR", "GLbyte", "signed char" },
        { "Boolean", "CHAR", "GLboolean", "unsigned char" },
        { "s", "SHORT", "GLshort", "short" },
        { "i", "INT", "GLint", "int" },
        { "Integer", "INT", "GLint", "int" },
        { "ub", "CHAR", "GLubyte", "unsigned char" },
        { "h", "SHORT", "GLhalf", "unsigned short" },
        { "us", "SHORT", "GLushort", "unsigned short" },
        { "ui", "INT", "GLuint", "unsigned int" },
        { "Nb", "CHAR", "GLbyte", "signed char" },
        { "Ns", "SHORT", "GLshort", "short" },
        { "Ni", "INT", "GLint", "int" },
        { "Nub", "CHAR", "GLubyte", "unsigned char" },
        { "Nus", "SHORT", "GLushort", "unsigned short" },
        { "Nui", "INT", "GLuint", "unsigned int" },
        
        { "f", "FLOAT", "GLfloat", "float" },
        { "Float", "FLOAT", "GLfloat", "float" },
        { "d", "DOUBLE", "GLdouble", "double" },
    };
    
    if (desc->nargs == 0)
        return 0;
    
    int i , j;
    
    if (strstr(desc->name, "glVertexAttribs") ||
        strstr(desc->name, "glProgramParameters") ||
        strstr(desc->name, "glProgramEnvParameters") ||
        strstr(desc->name, "glProgramLocalParameters") ||
        (strstr(desc->name, "glUniform") && (strstr(desc->name, "iv") || strstr(desc->name, "fv"))))
        return 0;
    
    static char signatures[N_ELEMENTS(my_tab)][N_FIELDS_IN_ARG_VECTOR][20] = {0};
    char signature[10];
    
    for(i=0;i<N_ELEMENTS(knownLastArgFuncs);i++) {
        if (strcmp(desc->name, knownLastArgFuncs[i].func_name) == 0) {
            if (p_signature_type_name) {
                *p_signature_type_name = knownLastArgFuncs[i].signature_type_name;
            }
            if (p_c_type_name) {
                if (strstr(knownLastArgFuncs[i].signature_type_name, "FLOAT"))
                    *p_c_type_name = "float";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "DOUBLE"))
                    *p_c_type_name = "double";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "UINT"))
                    *p_c_type_name = "unsigned int";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "INT"))
                    *p_c_type_name = "int";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "USHORT"))
                    *p_c_type_name = "unsigned short";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "SHORT"))
                    *p_c_type_name = "short";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "UCHAR"))
                    *p_c_type_name = "unsigned char";
                else if (strstr(knownLastArgFuncs[i].signature_type_name, "CHAR"))
                    *p_c_type_name = "char";
                else
                    assert(0);
            }
            return 1;
        }
    }
    
    for(i=0;i<N_ELEMENTS(my_tab);i++) {
        for(j=1;j<=N_FIELDS_IN_ARG_VECTOR;j++) {
            if (strstr(desc->name, "glIndex") && strstr(desc->name, "v"))
                sprintf(signature, "%sv", my_tab[i].letter);
            else
                sprintf(signature, "%d%sv", j, my_tab[i].letter);
            if (strstr(desc->name, signature) &&
                strstr(desc->args[desc->nargs - 1], my_tab[i].gl_c_type_name) &&
                strstr(desc->args[desc->nargs - 1], "*")) {
                if (p_signature_type_name) {
                    if (signatures[i][j-1][0] == 0)
                        sprintf(signatures[i][j-1], "TYPE_%d%s", j, my_tab[i].signature_type_name);
                    *p_signature_type_name = signatures[i][j-1];
                }
                if (p_c_type_name) *p_c_type_name = my_tab[i].c_type_name;
                return 1;
            }
        }
    }
    return 0;
}

static void print_server_side_argument(FILE* server_stub, int j, char* glType)
{
    const char* symbolic_type = get_type_string(glType);
    if (strcmp(symbolic_type, "TYPE_CHAR") == 0)
        fprintf(server_stub, "ARG_TO_CHAR(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_UNSIGNED_CHAR") == 0)
        fprintf(server_stub, "ARG_TO_UNSIGNED_CHAR(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_SHORT") == 0)
        fprintf(server_stub, "ARG_TO_SHORT(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_UNSIGNED_SHORT") == 0)
        fprintf(server_stub, "ARG_TO_UNSIGNED_SHORT(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_INT") == 0)
        fprintf(server_stub, "ARG_TO_INT(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_UNSIGNED_INT") == 0)
        fprintf(server_stub, "ARG_TO_UNSIGNED_INT(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_FLOAT") == 0)
        fprintf(server_stub, "ARG_TO_FLOAT(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_16FLOAT") == 0)
        fprintf(server_stub, "(const float*)(args[%d])", j);
    else if (strcmp(symbolic_type, "TYPE_DOUBLE") == 0)
        fprintf(server_stub, "ARG_TO_DOUBLE(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_16DOUBLE") == 0)
        fprintf(server_stub, "(const double*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_128UCHAR") == 0)
        fprintf(server_stub, "(unsigned char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_128UCHAR") == 0)
        fprintf(server_stub, "(const unsigned char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_NULL_TERMINATED_STRING") == 0)
        fprintf(server_stub, "(const char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_SHORT") == 0)
        fprintf(server_stub, "(const short*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_UNSIGNED_SHORT") == 0)
        fprintf(server_stub, "(const unsigned short*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_INT") == 0)
        fprintf(server_stub, "(const int*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_UNSIGNED_INT") == 0)
        fprintf(server_stub, "(const unsigned int*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_FLOAT") == 0)
        fprintf(server_stub, "(const float*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_DOUBLE") == 0)
        fprintf(server_stub, "(const double*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_CHAR") == 0)
        fprintf(server_stub, "(const char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_SIGNED_CHAR") == 0)
        fprintf(server_stub, "(const signed char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_VOID") == 0)
        fprintf(server_stub, "(const void*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_ARRAY_UNSIGNED_CHAR") == 0)
        fprintf(server_stub, "(const unsigned char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_SHORT") == 0)
        fprintf(server_stub, "(short*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_UNSIGNED_SHORT") == 0)
        fprintf(server_stub, "(unsigned short*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_INT") == 0)
        fprintf(server_stub, "(int*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_UNSIGNED_INT") == 0)
        fprintf(server_stub, "(unsigned int*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_FLOAT") == 0)
        fprintf(server_stub, "(float*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_DOUBLE") == 0)
        fprintf(server_stub, "(double*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_VOID") == 0)
        fprintf(server_stub, "(void*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_CHAR") == 0)
        fprintf(server_stub, "(char*)(args[%d])", j);
    else if ( strcmp(symbolic_type, "TYPE_OUT_ARRAY_UNSIGNED_CHAR") == 0)
        fprintf(server_stub, "(unsigned char*)(args[%d])", j);
    else {
        fprintf(stderr, "Unknown : %s\n", symbolic_type);
        assert(0);
    }
}

static const char* func_dealt_by_hand[500] = { NULL };

static const char* ignore_func[] = {
    "glMapBuffer",
    "glMapBufferARB",
    "glGetBufferPointerv",
    "glGetBufferPointervARB",
    "glUnmapBuffer",
    "glUnmapBufferARB",
    "glVertexWeightfEXT",
    "glVertexWeightfvEXT",
    "glVertexWeightPointerEXT",
    "glRectdv",
    "glRectfv",
    "glRectiv",
    "glRectsv",
    "glGetPointerv",
    "glGetPointervEXT",
    "glGetVertexAttribPointerv",
    "glGetVertexAttribPointervARB",
    "glGetVertexAttribPointervNV",
    "glMapObjectBufferATI",
    "glUnmapObjectBufferATI",

    "glColorTable",
    "glColorTableEXT",
    "glColorSubTable",
    "glColorSubTableEXT",
    "glGetColorTable",
    "glGetColorTableEXT",
    "glConvolutionFilter1D",
    "glConvolutionFilter1DEXT",
    "glConvolutionFilter2D",
    "glConvolutionFilter2DEXT",
    "glGetConvolutionFilter",
    "glGetConvolutionFilterEXT",
    "glGetSeparableFilter",
    "glGetSeparableFilterEXT",
    "glSeparableFilter2D",
    "glSeparableFilter2DEXT",
    "glGetHistogram",
    "glGetHistogramEXT",
    "glGetMinmax",
    "glGetMinmaxEXT",
    "glTexFilterFuncSGIS",
    "glGetTexFilterFuncSGIS",

    "glPixelDataRangeNV",
    "glFlushPixelDataRangeNV",
    "glVertexArrayRangeNV",
    "glFlushVertexArrayRangeNV",

    "glGetString",
    NULL,
};

void get_func_dealt_by_hand( char * path )
{
    FILE* f;
    char buffer[256], *filename;
    int i = 0;
    char* c;
    filename=malloc(strlen(path)+50);
    sprintf(filename, "%s/gl_func_perso.h",path );
    f = fopen(filename, "r");
    assert(f);
    while(fgets(buffer, 256, f)) {
        if (strstr(buffer, "MAGIC_MACRO(")) {
            func_dealt_by_hand[i] = strdup(strstr(buffer, "MAGIC_MACRO(") + strlen("MAGIC_MACRO("));
            * strstr(func_dealt_by_hand[i], ")") = 0;
            c = strstr(func_dealt_by_hand[i], "_");
            if (c && c != func_dealt_by_hand[i]) *c = 0;
            i ++;
        }
    }
    fclose(f);
    free(filename);
    
    int j = 0;
    while(ignore_func[j]) {
        func_dealt_by_hand[i] = ignore_func[j];
        i++;
        j++;
    }
}

static const char* just_for_server_side_list[] = {
    "glGetBooleanv",
    "glGetIntegerv",
    "glGetFloatv",
    "glGetDoublev",
    "glViewport",
    "glGenBuffers",
    "glGenBuffersARB",
    "glBufferData",
    "glBufferDataARB",
    "glBufferSubData",
    "glBufferSubDataARB",
    "glGetBufferSubData",
    "glGetBufferSubDataARB",
    "glCallLists",
    "glGetPixelMapfv",
    "glGetPixelMapuiv",
    "glGetPixelMapusv",
    "glMap1f",
    "glMap1d",
    "glMap2f",
    "glMap2d",
    "glGetMapdv",
    "glGetMapfv",
    "glGetMapiv",
    "glGenTextures",
    "glGenTexturesEXT",
    "glTexImage1D",
    "glTexImage1DEXT",
    "glTexImage2D",
    "glTexImage2DEXT",
    "glTexImage3D",
    "glTexImage3DEXT",
    "glTexSubImage1D",
    "glTexSubImage1DEXT",
    "glTexSubImage2D",
    "glTexSubImage2DEXT",
    "glTexSubImage3D",
    "glTexSubImage3DEXT",
    "glSelectBuffer",
    "glFeedbackBuffer",
    "glGetCompressedTexImageARB",
    "glGetCompressedTexImage",
    "glCompressedTexImage1DARB",
    "glCompressedTexImage1D",
    "glCompressedTexImage2DARB",
    "glCompressedTexImage2D",
    "glCompressedTexImage3DARB",
    "glCompressedTexImage3D",
    "glCompressedTexSubImage1DARB",
    "glCompressedTexSubImage1D",
    "glCompressedTexSubImage2DARB",
    "glCompressedTexSubImage2D",
    "glCompressedTexSubImage3DARB",
    "glCompressedTexSubImage3D",
    "glGetTexParameterfv",
    "glBitmap",
    "glGetTexImage",
    "glReadPixels",
    "glDrawPixels",
    "glInterleavedArrays",
    "glVertexPointer",
    "glVertexPointerEXT",
    "glNormalPointer",
    "glNormalPointerEXT",
    "glIndexPointer",
    "glIndexPointerEXT",
    "glColorPointer",
    "glColorPointerEXT",
    "glSecondaryColorPointer",
    "glSecondaryColorPointerEXT",
    "glTexCoordPointer",
    "glTexCoordPointerEXT",
    "glEdgeFlagPointer",
    "glEdgeFlagPointerEXT",
    "glFogCoordPointer",
    "glFogCoordPointerEXT",
    "glWeightPointerARB",
    "glMatrixIndexPointerARB",
    "glMatrixIndexubvARB",
    "glMatrixIndexusvARB",
    "glMatrixIndexuivARB",
    "glVertexAttribPointer",
    "glVertexAttribPointerARB",
    "glElementPointerATI",
    "glDrawElements",
    "glDrawRangeElements",
    "glDrawRangeElementsEXT",
    "glMultiDrawArrays",
    "glMultiDrawArraysEXT",
    "glMultiDrawElements",
    "glMultiDrawElementsEXT",
    "glShaderSource",
    "glShaderSourceARB",
    "glGetProgramInfoLog",
    "glGetProgramStringARB",
    "glGetProgramStringNV",
    "glGetInfoLogARB",
    "glGetAttachedObjectsARB",
    "glGetAttachedShaders",
    "glGetActiveUniform",
    "glGetActiveUniformARB",
    "glGetActiveVaryingNV",
    "glGetUniformfv",
    "glGetUniformfvARB",
    "glGetUniformiv",
    "glGetUniformivARB",
    "glGetUniformuivEXT",
    "glGetShaderSource",
    "glGetShaderSourceARB",
    "glGetShaderInfoLog",
    "glNewObjectBufferATI",
    "glUpdateObjectBufferATI",
    "glGetActiveAttrib",
    "glGetActiveAttribARB",
    "glTangentPointerEXT",
    "glBinormalPointerEXT",
    "glGenSymbolsEXT",
    "glSetLocalConstantEXT",
    "glSetInvariantEXT",
    "glVariantbvEXT",
    "glVariantsvEXT",
    "glVariantivEXT",
    "glVariantfvEXT",
    "glVariantdvEXT",
    "glVariantubvEXT",
    "glVariantusvEXT",
    "glVariantuivEXT",
    "glVariantPointerEXT",
    "glGetVariantBooleanvEXT",
    "glGetVariantIntegervEXT",
    "glGetVariantFloatvEXT",
    "glGetVariantPointervEXT",
    "glGetInvariantBooleanvEXT",
    "glGetInvariantIntegervEXT",
    "glGetInvariantFloatvEXT",
    "glGetLocalConstantBooleanvEXT",
    "glGetLocalConstantIntegervEXT",
    "glGetLocalConstantFloatvEXT",
    "glGetDetailTexFuncSGIS",
    "glGetSharpenTexFuncSGIS",
    NULL,
};

static int just_for_server_side_func(char* funcname)
{
    int i;
    for(i=0;just_for_server_side_list[i];i++) {
        if (strcmp(just_for_server_side_list[i], funcname) == 0)
            return 1;
    }
    return 0;
}

int parse(FILE* f, FuncDesc* funcDesc, int funcDescCount, int ignoreEXT, char *path)
{
    char buffer[256];
    while(fgets(buffer, 256, f)) {
        if (strncmp(buffer, "GLAPI", 5) == 0 && strstr(buffer, "APIENTRY") && strstr(buffer, "(")) {
            int i = 0;
            int skip = 0;
            if (func_dealt_by_hand[0] == 0) {
                get_func_dealt_by_hand(path);
            }
            while (func_dealt_by_hand[i]) {
                if (strstr(buffer, func_dealt_by_hand[i])) {
                    skip = 1;
                    break;
                }
                i++;
            }
            if (skip)
                continue;
            
            char** args = malloc(15 * sizeof(char*));
            int narg = 0;
            char* type = buffer + 6;
            char* n = strstr(type, "GLAPIENTRY") ? strstr(type, "GLAPIENTRY") : strstr(type, "APIENTRY");
            int skip_length = strstr(type, "GLAPIENTRY") ? 11 : 9;
            n[-1] = 0;
            type = strdup(type);
            n += skip_length;
            char* fonc = n;
            n = strstr(n, "(");
            if (n[-1] == ' ') n[-1] = 0;
            n[0] = 0;
            fonc = strdup(fonc);
            /*if (strstr(fonc, "glLockArraysEXT") || strstr(fonc, "glUnlockArraysEXT"))
             {
             }
             else*/
            if (ignoreEXT == 1 && isExtByName(fonc)) {
                free(type);
                free(fonc);
                continue;
            }
            n++;
            while(1) {
                char* virg = strstr(n, ",");
                if (virg) {
                    args[narg] = n;
                    virg[0] = 0;
                    args[narg] = get_arg_type(args[narg]);
                    narg++;
                    n = virg+1;
                } else
                    break;
            }
            while (strstr(n, ")") == 0) {
                fgets(buffer, 256, f);
                n = buffer;
                while(1) {
                    char* virg = strstr(n, ",");
                    if (virg) {
                        args[narg] = n;
                        virg[0] = 0;
                        args[narg] = get_arg_type(args[narg]);
                        narg++;
                        n = virg+1;
                    } else
                        break;
                }
            }
            char* par = strstr(n, ")");
            args[narg] = n;
            par[0] = 0;
            args[narg] = get_arg_type(args[narg]);
            narg++;
            
            /*printf("%s %s (", type, fonc);
             for(i=0;i<narg;i++)
             {
             printf("%s,", args[i]);
             }
             printf(")\n");*/
            
            for(i=0;i<funcDescCount;i++) {
                if (strcmp(funcDesc[i].name, fonc) == 0) {
                    if (ignoreEXT == 0)
                        funcDesc[i].isExt = 1;
                    break;
                }
            }
            if (i == funcDescCount) {
                funcDesc[funcDescCount].type = type;
                funcDesc[funcDescCount].name = fonc;
                funcDesc[funcDescCount].nargs = narg;
                funcDesc[funcDescCount].args = args;
                funcDesc[funcDescCount].isExt = ignoreEXT == 0;
                funcDescCount++;
            } else {
                free(fonc);
                free(args);
                free(type);
            }
            /*
             for(i=0;i<narg;i++)
             {
             free(args[i]);
             }
             free(fonc);
             free(type);*/
        }
    }
    return funcDescCount;
}

typedef struct {
    char* str;
    int i;
} StringIntStruct;

StringIntStruct argDependingOnPreviousArgTab[] = {
    {"glLoadProgramNV", 3},
    {"ProgramNamedParameter", 2},
    {"glDeleteBuffers", 1},
    {"glDrawBuffers", 1},
    {"glGenPrograms", 1},
    {"glDeletePrograms", 1},
    {"glGenQueries", 1},
    {"glDeleteQueries", 1},
    {"glGenFencesNV", 1},
    {"glDeleteFencesNV", 1},
    {"glGenOcclusionQueriesNV", 1},
    {"glDeleteOcclusionQueriesNV", 1},
    {"glRequestResidentProgramsNV", 1},
    {"glDeleteTextures", 1},
    {"glGenFramebuffersEXT", 1},
    {"glDeleteFramebuffersEXT", 1},
    {"glGenRenderbuffersEXT", 1},
    {"glDeleteRenderbuffersEXT", 1},
    {"glUniform1fv", 2},
    {"glUniform2fv", 2},
    {"glUniform3fv", 2},
    {"glUniform4fv", 2},
    {"glUniform1iv", 2},
    {"glUniform2iv", 2},
    {"glUniform3iv", 2},
    {"glUniform4iv", 2},
    {"glUniform1uivEXT", 2},
    {"glUniform2uivEXT", 2},
    {"glUniform3uivEXT", 2},
    {"glUniform4uivEXT", 2},
    {"glProgramParameters4fvNV", 3},
    {"glProgramParameters4dvNV", 3},
    {"glProgramLocalParameters4fvEXT", 3},
    {"glProgramLocalParametersI4ivNV", 3},
    {"glProgramLocalParametersI4uivNV", 3},
    {"glProgramEnvParameters4fvEXT", 3},
    {"glProgramEnvParametersI4ivNV", 3},
    {"glProgramEnvParametersI4uivNV", 3},
    {"glAreProgramsResidentNV", 1} ,
    {"glAreProgramsResidentNV", 2} ,
    {"glAreTexturesResident", 1} ,
    {"glAreTexturesResident", 2} ,
    {"glPrioritizeTextures", 1} ,
    {"glPrioritizeTextures", 2} ,
    {"glProgramStringARB", 3} ,

    {"glVertexAttribs", 2},

    {"glUniformMatrix", 3 },

    {"glGetVertexAttribfv", 2},
    {"glGetVertexAttribiv", 2},
    {"glGetVertexAttribdv", 2},
    {"glGetVertexAttribIivEXT", 2},
    {"glGetVertexAttribIuivEXT", 2},

    {"glPointParameterfv", 1},
    {"glPointParameteriv", 1},

    {"glWeightbvARB", 1},
    {"glWeightsvARB", 1},
    {"glWeightivARB", 1},
    {"glWeightfvARB", 1},
    {"glWeightdvARB", 1},
    {"glWeightubvARB", 1},
    {"glWeightusvARB", 1},
    {"glWeightuivARB", 1},

    {"glTexEnvfv", 2},
    {"glTexEnviv", 2},
    {"glGetTexEnvfv", 2},
    {"glGetTexEnviv", 2},
    {"glTexGendv", 2},
    {"glTexGenfv", 2},
    {"glTexGeniv", 2},
    {"glGetTexGendv", 2},
    {"glGetTexGenfv", 2},
    {"glGetTexGeniv", 2},

    {"glLightfv", 2},
    {"glLightiv", 2},
    {"glGetLightfv", 2},
    {"glGetLightiv", 2},
    {"glFragmentLightfvSGIX", 2},
    {"glFragmentLightivSGIX", 2},
    {"glGetFragmentLightfvSGIX", 2},
    {"glGetFragmentLightivSGIX", 2},

    {"glLightModelfv", 1},
    {"glLightModeliv", 1},
    {"glFragmentLightModelfvSGIX", 1},
    {"glFragmentLightModelivSGIX", 1},

    {"glMaterialfv", 2},
    {"glMaterialiv", 2},
    {"glGetMaterialfv", 2},
    {"glGetMaterialiv", 2},
    {"glFragmentMaterialfvSGIX", 2},
    {"glFragmentMaterialivSGIX", 2},
    {"glGetFragmentMaterialfvSGIX", 2},
    {"glGetFragmentMaterialivSGIX", 2},

    {"glFogiv", 1},
    {"glFogfv", 1},

    {"glTexParameterfv", 2},
    {"glTexParameteriv", 2},
    {"glGetTexParameterfv", 2},
    {"glGetTexParameteriv", 2},

    {"glTexParameterIivEXT", 2},
    {"glTexParameterIuivEXT", 2},
    {"glGetTexParameterIivEXT", 2},
    {"glGetTexParameterIuivEXT", 2},

    {"glPixelMapfv", 2},
    {"glPixelMapuiv", 2},
    {"glPixelMapusv", 2},

    {"glDetailTexFuncSGIS", 2 },
    {"glSharpenTexFuncSGIS", 2 },

    {"glSpriteParameterfvSGIX", 1 },
    {"glSpriteParameterivSGIX", 1 },

    {"ConvolutionParameter", 2},

    {"glProgramBufferParametersfvNV", 4},
    {"glProgramBufferParametersIivNV", 4},
    {"glProgramBufferParametersIuivNV", 4},

    {"glTransformFeedbackAttribsNV", 1},
    {"glTransformFeedbackVaryingsNV", 2},
};

int is_arg_of_length_depending_on_previous_args(FuncDesc* funcDesc, int j)
{
    int i;
    if (strstr(funcDesc->args[j], "*") == NULL)
        return 0;
    for(i=0;i< N_ELEMENTS(argDependingOnPreviousArgTab); i++) {
        if (strstr(funcDesc->name, argDependingOnPreviousArgTab[i].str) && j == argDependingOnPreviousArgTab[i].i)
            return 1;
    }
    return 0;
}

static void fprintf_prototype_args(FILE* f, FuncDesc* funcDesc)
{
    int j;
    if (!funcDesc->nargs) {
        fprintf(f, "void");
    } else {
        for(j=0;j<funcDesc->nargs;j++) {
            if (j != 0) fprintf(f,", ");
            if (strstr(funcDesc->args[j], "[16]")) {
                if (strstr(funcDesc->args[j], "float")) {
                    fprintf(f, "const GLfloat arg_%d[16]", j);
                } else if (strstr(funcDesc->args[j], "double")) {
                    fprintf(f, "const GLdouble arg_%d[16]", j);
                } else {
                    exit(-1);
                }
            }
            else if (strstr(funcDesc->args[j], "[128]") && strstr(funcDesc->args[j], "GLubyte"))
                fprintf(f, (strstr(funcDesc->args[j], "const")) ? "const GLubyte* arg_%d" : "GLubyte* arg_%d", j);
            else
                fprintf(f, "%s arg_%d", funcDesc->args[j], j);
        }
    }
}

int main(int argc, char* argv[])
{
    FuncDesc funcDesc[3000];
    int funcDescCount = 0;
    char *path,*filename;
    FILE* f;
    if (argc != 2) {
        printf("usage: ./parse_gl_h sourcepath\n");
        return 1;
    }
    path=argv[1];
    filename=malloc(strlen(path)+50);
    sprintf(filename, "%s/mesa_gl.h",path );
    
    f = fopen(filename, "r");
    assert(f);
    /*if (!f)
     f = fopen("/usr/include/GL/gl.h", "r");*/
    funcDescCount = parse(f, funcDesc, 0, 1, path);
    fclose(f);
    
    sprintf(filename, "%s/mesa_glext.h",path );
    f = fopen(filename, "r");
    assert(f);
    /*if (!f)
     f = fopen("/usr/include/GL/glext.h", "r");*/
    funcDescCount = parse(f, funcDesc, funcDescCount, 0, path);
    fclose(f);
    free(filename);
    
    FILE* header = fopen("gl_func.h", "w");
    FILE* client_stub = fopen("client_stub.c", "w");
    FILE* server_stub = fopen("server_stub.c", "w");
    
    fprintf(header, "/* This is a generated file. DO NOT EDIT ! */\n\n");
    fprintf(header, "#define COMPOSE(x,y) x##y\n");
    fprintf(header, "#define MAGIC_MACRO(x)  COMPOSE(x,_func)\n");
    fprintf(header, "enum {\n"
            "#include \"gl_func_perso.h\"\n");
    
    fprintf(client_stub, "/* This is a generated file. DO NOT EDIT ! */\n\n");
    
    fprintf(server_stub, "/* This is a generated file. DO NOT EDIT ! */\n\n");
    
    int i;
    for(i=0;i<funcDescCount;i++) {
        funcDesc[i].ok = 0;
        char* name = funcDesc[i].name;
        char* type = funcDesc[i].type;
        if (!strcmp(type, "void")   || !strcmp(type, "GLboolean") == 0 ||
            !strcmp(type, "GLuint") || !strcmp(type, "GLint") == 0 ||
            !strcmp(type, "GLenum") || !strcmp(type, "GLhandleARB") == 0 ||
            !strcmp(type, "GLhalf") || !strcmp(type, "GLhalfNV") == 0) {
            int pointer_of_unknown_size = 0;
            int j;
            
            if (funcDesc[i].nargs == 1 && strcmp(funcDesc[i].args[0], "void") == 0) {
                funcDesc[i].nargs = 0;
            }
            for(j=0;j<funcDesc[i].nargs-1;j++) {
                if (!is_arg_of_length_depending_on_previous_args(&funcDesc[i], j) &&
                    strstr(funcDesc[i].args[j], "const GLchar") == NULL &&
                    strstr(funcDesc[i].args[j], "[16]") == NULL) {
                    pointer_of_unknown_size |= strstr(funcDesc[i].args[j], "*") != NULL;
                    pointer_of_unknown_size |= strstr(funcDesc[i].args[j], "[") != NULL;
                }
            }
            
            if (pointer_of_unknown_size == 0) {
                char* signature_type_name;
                if (is_known_arg_vector(&funcDesc[i], &signature_type_name, NULL)) {
                    if (strstr(signature_type_name, "TYPE_OUT"))
                        funcDesc[i].has_out_parameters = 1;
                } else {
                    if (funcDesc[i].nargs-1 >= 0) {
                        j = funcDesc[i].nargs-1;
                        if (!is_arg_of_length_depending_on_previous_args(&funcDesc[i], j) &&
                            strstr(funcDesc[i].args[j], "const GLchar") == NULL &&
                            strstr(funcDesc[i].args[j], "[16]") == NULL) {
                            pointer_of_unknown_size |= strstr(funcDesc[i].args[j], "*") != NULL;
                            pointer_of_unknown_size |= strstr(funcDesc[i].args[j], "[") != NULL;
                        }
                    }
                }
            }
            if (pointer_of_unknown_size && funcDesc[i].nargs == 1) {
                if (strstr(funcDesc[i].name, "Matrixf") || strstr(funcDesc[i].name, "Matrixd")) {
                    free(funcDesc[i].args[0]);
                    if (strstr(funcDesc[i].name, "Matrixf"))
                        funcDesc[i].args[0] = strdup("GLfloat m[16]");
                    else
                        funcDesc[i].args[0] = strdup("GLdouble m[16]");
                    pointer_of_unknown_size = 0;
                } else if (strcmp(funcDesc[i].name, "glPolygonStipple") == 0) {
                    free(funcDesc[i].args[0]);
                    funcDesc[i].args[0] = strdup("const GLubyte mask[128]");
                    pointer_of_unknown_size = 0;
                } else if (strcmp(funcDesc[i].name, "glGetPolygonStipple") == 0) {
                    free(funcDesc[i].args[0]);
                    funcDesc[i].args[0] = strdup("GLubyte mask[128]");
                    funcDesc[i].has_out_parameters = 1;
                    pointer_of_unknown_size = 0;
                }
            }
            if (just_for_server_side_func(name) || pointer_of_unknown_size == 0) {
                fprintf(header, "  %s_func,\n", funcDesc[i].name);
                funcDesc[i].ok = 1;
                if (just_for_server_side_func(name))
                    funcDesc[i].just_for_server_side = 1;
                for(j=0;j<funcDesc[i].nargs;j++) {
                    if (strstr(get_type_string(funcDesc[i].args[j]), "OUT"))
                        funcDesc[i].has_out_parameters = 1;
                }
            } else {
                //fprintf(stderr, "not handled either manually or automatically : %s\n", funcDesc[i].name);
            }
        } else {
            fprintf(stderr,
                    "unsupported return type \"%s\" for function \"%s\"\n",
                    type, name);
        }
    }
    
    fprintf(header, "  GL_N_CALLS\n};\n");
    
    fprintf(server_stub, "static void execute_func(int func_number, target_phys_addr_t* args, int* pret_int, char* pret_char)\n");
    fprintf(server_stub, "{\n");
    fprintf(server_stub, "  switch(func_number)\n");
    fprintf(server_stub, "  {\n");
    
    for(i=0;i<funcDescCount;i++) {
        if (funcDesc[i].ok) {
            fprintf(header, "static const int %s_signature[] = { %s, %d, ",
                    funcDesc[i].name,
                    get_type_string(funcDesc[i].type),
                    funcDesc[i].has_out_parameters);
            fprintf(header, "%d", funcDesc[i].nargs);
            int j;
            char* signature_type_name;
            int n_args_to_check = is_known_arg_vector(&funcDesc[i], &signature_type_name, NULL) ? funcDesc[i].nargs - 1 : funcDesc[i].nargs;
            
            for(j=0;j<n_args_to_check;j++) {
                if (is_arg_of_length_depending_on_previous_args(&funcDesc[i], j)) {
                    fprintf(header, ", %s_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS", get_type_string(funcDesc[i].args[j]));
                } else
                    fprintf(header, ", %s", get_type_string(funcDesc[i].args[j]));
            }
            
            if (is_known_arg_vector(&funcDesc[i], &signature_type_name, NULL)) {
                fprintf(header, ", %s", signature_type_name);
            }
            fprintf(header, "};\n");
            
            
            if (funcDesc[i].just_for_server_side == 0) {
                /*if (isExt(&funcDesc[i]))
                    fprintf(client_stub, "GLAPI %s APIENTRY EXT_FUNC(%s) (", funcDesc[i].type, funcDesc[i].name);
                else*/
                    fprintf(client_stub, "GLAPI %s APIENTRY %s(", funcDesc[i].type, funcDesc[i].name);
                fprintf_prototype_args(client_stub, &funcDesc[i]);
                fprintf(client_stub, ")\n");
                fprintf(client_stub, "{\n");
                if (strcmp(funcDesc[i].type, "void") != 0) {
                    fprintf(client_stub, "  %s ret;\n", funcDesc[i].type);
                    /*if (isExt(&funcDesc[i]))
                        fprintf(client_stub, "  CHECK_PROC_WITH_RET(%s);\n", funcDesc[i].name);*/
                } else {
                    /*if (isExt(&funcDesc[i]))
                        fprintf(client_stub, "  CHECK_PROC(%s);\n", funcDesc[i].name);*/
                }
                
                /*
                 fprintf(client_stub, "  do_opengl_call(%s_func, %s",
                 funcDesc[i].name, (strcmp(funcDesc[i].type, "void") == 0) ? "NULL" : "&ret");
                 for(j=0;j<funcDesc[i].nargs;j++)
                 {
                 fprintf(client_stub, ", arg_%d", j);
                 }
                 fprintf(client_stub, ");\n");
                 */
                
                if (funcDesc[i].nargs) {
                    fprintf(client_stub, "  long args[] = { ");
                    for(j=0;j<funcDesc[i].nargs;j++) {
                        if (j > 0) fprintf(client_stub, ", ");
                        if (strstr(funcDesc[i].args[j], "*")) {
                            fprintf(client_stub, "POINTER_TO_ARG(arg_%d)", j);
                        } else {
                            const char* symbolic_type = get_type_string(funcDesc[i].args[j]);
                            if (strcmp(symbolic_type, "TYPE_CHAR") == 0)
                                fprintf(client_stub, "CHAR_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_UNSIGNED_CHAR") == 0)
                                fprintf(client_stub, "UNSIGNED_CHAR_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_SHORT") == 0)
                                fprintf(client_stub, "SHORT_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_UNSIGNED_SHORT") == 0)
                                fprintf(client_stub, "UNSIGNED_SHORT_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_INT") == 0)
                                fprintf(client_stub, "INT_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_UNSIGNED_INT") == 0)
                                fprintf(client_stub, "UNSIGNED_INT_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_FLOAT") == 0)
                                fprintf(client_stub, "FLOAT_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_16FLOAT") == 0)
                                fprintf(client_stub, "POINTER_TO_ARG");
                            else if (strcmp(symbolic_type, "TYPE_DOUBLE") == 0)
                                fprintf(client_stub, "DOUBLE_TO_ARG");
                            else if ( strcmp(symbolic_type, "TYPE_16DOUBLE") == 0)
                                fprintf(client_stub, "POINTER_TO_ARG");
                            else if ( strcmp(symbolic_type, "TYPE_128UCHAR") == 0 || strcmp(symbolic_type, "TYPE_OUT_128UCHAR") == 0)
                                fprintf(client_stub, "POINTER_TO_ARG");
                            else {
                                fprintf(stderr, "Unknown : %s\n", symbolic_type);
                                assert(0);
                            }
                            fprintf(client_stub, "(arg_%d)", j);
                        }
                    }
                    fprintf(client_stub, "};\n");
                }
                
                fprintf(client_stub, "  do_opengl_call(%s_func, %s, %s, NULL);\n",
                        funcDesc[i].name, (strcmp(funcDesc[i].type, "void") == 0) ? "NULL" : "&ret",
                        (funcDesc[i].nargs) ? "args" : "NULL");
                
                if (strcmp(funcDesc[i].type, "void") != 0) {
                    fprintf(client_stub, "  return ret;\n");
                }
                fprintf(client_stub, "}\n\n");
            }
            
            fprintf(server_stub, "    case %s_func:\n", funcDesc[i].name);
            fprintf(server_stub, "    {\n");
            
            if (isExt(&funcDesc[i])) {
                fprintf(server_stub, "      GET_EXT_PTR(%s, %s, (", funcDesc[i].type, funcDesc[i].name);
                fprintf_prototype_args(server_stub, &funcDesc[i]);
                fprintf(server_stub, "));\n");
            }
            
            fprintf(server_stub, "      ");
            
            if (strcmp(funcDesc[i].type, "void") == 0)
                ;
            else if (strcmp(get_type_string(funcDesc[i].type), "TYPE_INT") == 0 ||
                     strcmp(get_type_string(funcDesc[i].type), "TYPE_UNSIGNED_INT") == 0)
                fprintf(server_stub, "*pret_int = ");
            else if (strcmp(get_type_string(funcDesc[i].type), "TYPE_CHAR") == 0 ||
                     strcmp(get_type_string(funcDesc[i].type), "TYPE_UNSIGNED_CHAR") == 0)
                fprintf(server_stub, "*pret_char = ");
            else {
                fprintf(stderr, "unknown ret type = %s\n", get_type_string(funcDesc[i].type));
                exit(-1);
            }
            /*if (strstr(funcDesc[i].name, "EXT"))
             {
             char* dup = strdup(funcDesc[i].name);
             *strstr(dup, "EXT") = 0;
             fprintf(server_stub, "%s(", dup);
             free(dup);
             }
             else*/
            { 
                if (isExt(&funcDesc[i]))
                    fprintf(server_stub, "ptr_func_%s(", funcDesc[i].name);
                else
                    fprintf(server_stub, "%s(", funcDesc[i].name);
            }
            char* c_type_name;
            if (is_known_arg_vector(&funcDesc[i], NULL, &c_type_name)) {
                for(j=0;j<funcDesc[i].nargs - 1;j++) {
                    if (j != 0) fprintf(server_stub,", ");
                    print_server_side_argument(server_stub, j, funcDesc[i].args[j]);
                }
                if (j != 0) fprintf(server_stub,", ");
                if (strstr(funcDesc[i].args[funcDesc[i].nargs - 1], "const"))
                    fprintf(server_stub, "(const %s*)args[%d]", c_type_name, j);
                else
                    fprintf(server_stub, "(%s*)args[%d]", c_type_name, j);
            } else {
                for(j=0;j<funcDesc[i].nargs;j++) {
                    if (j != 0) fprintf(server_stub,", ");
                    print_server_side_argument(server_stub, j, funcDesc[i].args[j]);
                }
            }
            fprintf(server_stub, ");\n");
            
            fprintf(server_stub, "      break;\n");
            fprintf(server_stub, "    }\n");
        }
    }
    
    fprintf(server_stub, "    default:\n");
    fprintf(server_stub, "      fprintf(stderr, \"unknown=%%d\", func_number);\n");
    fprintf(server_stub, "      break;\n");
    fprintf(server_stub, "  }\n");
    fprintf(server_stub, "}\n");
    
    fprintf(header, "#undef MAGIC_MACRO\n");
    fprintf(header, "#define MAGIC_MACRO(x)  COMPOSE(x,_signature)\n");
    fprintf(header, "static const int* tab_opengl_calls[GL_N_CALLS] =\n");
    fprintf(header, "{\n");
    fprintf(header, "#include \"gl_func_perso.h\"\n");
    for(i=0;i<funcDescCount;i++) {
        if (funcDesc[i].ok) {
            fprintf(header, "  %s_signature,\n", funcDesc[i].name);
        }
    }
    fprintf(header, "};\n\n");
    
    fprintf(header, "#undef MAGIC_MACRO\n");
    fprintf(header, "#define MAGIC_MACRO(x)  #x\n");
    fprintf(header, "static const char* tab_opengl_calls_name[GL_N_CALLS] =\n");
    fprintf(header, "{\n");
    fprintf(header, "#include \"gl_func_perso.h\"\n");
    for(i=0;i<funcDescCount;i++) {
        if (funcDesc[i].ok) {
            fprintf(header, "  \"%s\",\n", funcDesc[i].name);
        }
    }
    fprintf(header, "};\n\n");
    
    fclose(header);
    fclose(server_stub);
    fclose(client_stub);
    
    return 0;
}
