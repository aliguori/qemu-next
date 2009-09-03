/*
 *  Guest-side implementation of GL/GLX API. Replacement of standard libGL.so
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

/* following env variables can be used to control runtime behavior:
 * GL_ERR_FILE ... log all output to the specified file instead of stderr
 * GL_DEBUG ...... log debug messages (disabled by default)
 */

/* gcc -Wall -g -O2 opengl_client.c -shared -o libGL.so.1 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>

#define GL_GLEXT_LEGACY
#include "mesa_gl.h"
#include "mesa_glext.h"

#include <dlfcn.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>

#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>

#include <mesa_glx.h>

#include "qemugl.h"

#define ENABLE_THREAD_SAFETY

#define GLENV_ERRFILE "GL_ERR_FILE"
#define GLENV_DEBUG   "GL_DEBUG"

/*void *pthread_getspecific(pthread_key_t key);
       int pthread_setspecific(pthread_key_t key, const void *value);*/

#define CHECK_ARGS(x, y) (1 / ((sizeof(x)/sizeof(x[0])) == (sizeof(y)/sizeof(y[0])) ? 1 : 0)) ? x : x, y

static void log_gl(const char *format, ...);
#define DEBUGLOG_GL(...) if (debug_gl) log_gl(__VA_ARGS__)
#define NOT_IMPLEMENTED() log_gl("%s: not implemented!\n", __FUNCTION__)

#define CONCAT(a, b) a##b
#define FUNC_NUM(funcname) CONCAT(funcname, _func)
//#define DEFINE_EXT(glFunc, paramsDecl, paramsCall)  GLAPI void APIENTRY CONCAT(glFunc,EXT) paramsDecl { glFunc paramsCall; }

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

typedef unsigned long target_ulong;

#include "opengl_func_parse.h"
#include "glgetv_cst.h"

typedef struct {
    int x;
    int y;
    int width;
    int height;
} ViewportStruct;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int map_state;
} WindowPosStruct;

typedef struct {
    int id;
    int datatype;
    int components;
} Symbol;

typedef struct {
    Symbol *tab;
    int count;
} Symbols;
   
typedef struct {
    int ref;
    Display *display;
    GLXContext context;
    GLXDrawable current_drawable;
    GLXDrawable current_read_drawable;
    GLXContext shareList;
    GLXPbuffer pbuffer;
    ViewportStruct viewport;
    int drawable_width;
    int drawable_height;
    int drawable_depth;
    Visual drawable_visual;
    int isAssociatedToFBConfigVisual;
    XShmSegmentInfo *shm_info;
    XImage *ximage;
    GC xgc;
    Symbols symbols;
} GLState;

static GLState *new_gl_state(void)
{
    GLState *state = malloc(sizeof(GLState));
    memset(state, 0, sizeof(GLState));
    return state;
}

/* The access to the following global variables shoud be done under the global lock */
static GLState *default_gl_state = NULL;
static int nbGLStates = 0;
static GLState **glstates = NULL;

/* func numbers below MUST match first & last GLX functions in gl_func_perso.h */
#define IS_GLX_CALL(x) (x >= glXChooseVisual_func && x <= glXSwapIntervalSGI_func)

#ifdef ENABLE_THREAD_SAFETY

/* Posix threading */
/* The concepts used here are coming directly from http://www.mesa3d.org/dispatch.html */

#define GET_CURRENT_THREAD() pthread_self()

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t key_current_gl_state;
static GLState *_mono_threaded_current_gl_state = NULL;
static pthread_t last_current_thread = 0;
static int _is_mt = 0;

static inline int is_mt()
{
    if (!_is_mt) {
        pthread_t current_thread = GET_CURRENT_THREAD();
        if (!last_current_thread) {
            last_current_thread = current_thread;
        }
        if (current_thread != last_current_thread)
        {
            _is_mt = 1;
            log_gl("-------- Two threads at least are doing OpenGL ---------\n");
            pthread_key_create(&key_current_gl_state, NULL);
        }
    }
    return _is_mt;
}

#define IS_MT() is_mt()

/* The idea here is that the first GL/GLX call made in each thread is necessary a GLX call */
/* So in the case where it's a GLX call we always take the lock and check if we're in MT case */
/* otherwise (regular GL call), we have to take the lock only in the MT case */
#define LOCK(func_number) \
do { \
    if (IS_GLX_CALL(func_number)) { \
        pthread_mutex_lock(&global_mutex); \
        IS_MT(); \
    } else if (IS_MT()) { \
        pthread_mutex_lock(&global_mutex); \
    } \
} while (0)

#define UNLOCK(func_number) \
do { \
    if (IS_GLX_CALL(func_number) || IS_MT()) { \
        pthread_mutex_unlock(&global_mutex); \
    } \
} while(0)

static void set_current_state(GLState *current_gl_state)
{
    if (IS_MT()) {
        pthread_setspecific(key_current_gl_state, current_gl_state);
    } else {
        _mono_threaded_current_gl_state = current_gl_state;
    }
}

static inline GLState *get_current_state(void)
{
    GLState *current_gl_state;
    if (IS_MT() == 1 && last_current_thread == GET_CURRENT_THREAD()) {
        _is_mt = 2;
        set_current_state(_mono_threaded_current_gl_state);
        _mono_threaded_current_gl_state = NULL;
    }
    current_gl_state = IS_MT()
        ? pthread_getspecific(key_current_gl_state)
        : _mono_threaded_current_gl_state;
    if (current_gl_state == NULL) {
        if (default_gl_state == NULL) {
            default_gl_state = new_gl_state();
        }
        current_gl_state = default_gl_state;
        set_current_state(current_gl_state);
    }
    return current_gl_state;
}

#define SET_CURRENT_STATE(_x) set_current_state(_x)

#else // ENABLE_THREAD_SAFETY
/* No support for threading */

#define LOCK(func_number)
#define UNLOCK(func_number) 
#define GET_CURRENT_THREAD() 0
#define IS_MT() 0
static GLState *current_gl_state = NULL;
static inline GLState *get_current_state(void)
{
    if (current_gl_state == NULL) {
        if (default_gl_state == NULL) {
            default_gl_state = new_gl_state();
        }
        return default_gl_state;
    }
    return current_gl_state;
}
#define SET_CURRENT_STATE(_x) current_gl_state = _x
#endif // ENABLE_THREAD_SAFETY

#define GET_CURRENT_STATE() GLState *state = get_current_state()

static FILE *get_err_file(void)
{
    static FILE *err_file = NULL;
    if (err_file == NULL) {
        char *custom = getenv(GLENV_ERRFILE);
        if (custom) {
            err_file = fopen(custom, "w");
        }
        if (err_file == NULL) {
            err_file = stderr;
        }
    }
    return err_file;
}

static void log_gl(const char *format, ...)
{
    va_list list;
    va_start(list, format);
#ifdef ENABLE_THREAD_SAFETY
    if (IS_MT()) {
        fprintf(get_err_file(), "[thread %p] : ", (void *)GET_CURRENT_THREAD());
    }
#endif
    vfprintf(get_err_file(), format, list);
    va_end(list);
}

typedef struct {
    int attrib;
    int value;
    int ret;
} glXGetConfigAttribs_s;

typedef struct {
    int val;
    char* name;
} glXAttrib;

#define VAL_AND_NAME(x) { x, #x }

static const glXAttrib tabRequestedAttribsPair[] = {
    VAL_AND_NAME(GLX_USE_GL),
    VAL_AND_NAME(GLX_BUFFER_SIZE),
    VAL_AND_NAME(GLX_LEVEL),
    VAL_AND_NAME(GLX_RGBA),
    VAL_AND_NAME(GLX_DOUBLEBUFFER),
    VAL_AND_NAME(GLX_STEREO),
    VAL_AND_NAME(GLX_AUX_BUFFERS),
    VAL_AND_NAME(GLX_RED_SIZE),
    VAL_AND_NAME(GLX_GREEN_SIZE),
    VAL_AND_NAME(GLX_BLUE_SIZE),
    VAL_AND_NAME(GLX_ALPHA_SIZE),
    VAL_AND_NAME(GLX_DEPTH_SIZE),
    VAL_AND_NAME(GLX_STENCIL_SIZE),
    VAL_AND_NAME(GLX_ACCUM_RED_SIZE),
    VAL_AND_NAME(GLX_ACCUM_GREEN_SIZE),
    VAL_AND_NAME(GLX_ACCUM_BLUE_SIZE),
    VAL_AND_NAME(GLX_ACCUM_ALPHA_SIZE),
    VAL_AND_NAME(GLX_CONFIG_CAVEAT),
    VAL_AND_NAME(GLX_X_VISUAL_TYPE),
    VAL_AND_NAME(GLX_TRANSPARENT_TYPE),
    VAL_AND_NAME(GLX_TRANSPARENT_INDEX_VALUE),
    VAL_AND_NAME(GLX_TRANSPARENT_RED_VALUE),
    VAL_AND_NAME(GLX_TRANSPARENT_GREEN_VALUE),
    VAL_AND_NAME(GLX_TRANSPARENT_BLUE_VALUE),
    VAL_AND_NAME(GLX_TRANSPARENT_ALPHA_VALUE), 
    VAL_AND_NAME(GLX_SLOW_CONFIG),
    VAL_AND_NAME(GLX_TRUE_COLOR),
    VAL_AND_NAME(GLX_DIRECT_COLOR),
    VAL_AND_NAME(GLX_PSEUDO_COLOR),
    VAL_AND_NAME(GLX_STATIC_COLOR),
    VAL_AND_NAME(GLX_GRAY_SCALE),
    VAL_AND_NAME(GLX_STATIC_GRAY),
    VAL_AND_NAME(GLX_TRANSPARENT_RGB),
    VAL_AND_NAME(GLX_TRANSPARENT_INDEX),
    VAL_AND_NAME(GLX_VISUAL_ID),
    VAL_AND_NAME(GLX_DRAWABLE_TYPE),
    VAL_AND_NAME(GLX_RENDER_TYPE),
    VAL_AND_NAME(GLX_X_RENDERABLE),
    VAL_AND_NAME(GLX_FBCONFIG_ID),
    VAL_AND_NAME(GLX_RGBA_TYPE),
    VAL_AND_NAME(GLX_COLOR_INDEX_TYPE),
    VAL_AND_NAME(GLX_MAX_PBUFFER_WIDTH),
    VAL_AND_NAME(GLX_MAX_PBUFFER_HEIGHT),
    VAL_AND_NAME(GLX_MAX_PBUFFER_PIXELS),
    VAL_AND_NAME(GLX_PRESERVED_CONTENTS),
    VAL_AND_NAME(GLX_FLOAT_COMPONENTS_NV),
    VAL_AND_NAME(GLX_SAMPLE_BUFFERS),
    VAL_AND_NAME(GLX_SAMPLES)
};

#define N_REQUESTED_ATTRIBS (sizeof(tabRequestedAttribsPair)/sizeof(tabRequestedAttribsPair[0]))

static int *getTabRequestedAttribsInt(void)
{
    static int tabRequestedAttribsInt[N_REQUESTED_ATTRIBS] = {0};
    if (tabRequestedAttribsInt[0] == 0) {
        int i;
        for (i = 0; i < N_REQUESTED_ATTRIBS; i++) {
            tabRequestedAttribsInt[i] = tabRequestedAttribsPair[i].val;
        }
    }
    return tabRequestedAttribsInt;
}

#define N_MAX_ATTRIBS (N_REQUESTED_ATTRIBS + 10)

typedef struct {
    int                   visualid;
    glXGetConfigAttribs_s attribs[N_MAX_ATTRIBS];
    int                   nbAttribs;
} glXConfigs_s;

#define N_MAX_CONFIGS 80

static glXConfigs_s configs[N_MAX_CONFIGS];
static int nbConfigs = 0;

static int pagesize = 0;
static int debug_gl = 0;

#include <sys/types.h>
#include <sys/stat.h>

#ifdef QEMUGL_MODULE
#include <sys/ioctl.h>
#include <fcntl.h>
int glfd = 0;
#else
#ifdef __arm__
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)
unsigned int *virt_addr;
#endif
#endif
    
static int call_opengl(int func_number, int pid, void *ret_string, void *args,
                       void *args_size)
{
#ifdef QEMUGL_MODULE
    unsigned int devargs[5] = {func_number, pid, (unsigned int)ret_string,
                               (unsigned int)args, (unsigned int)args_size};
    int result = 0;
    if (!(result = ioctl(glfd, QEMUGL_FIORNCMD, &devargs))) {
        ioctl(glfd, QEMUGL_FIORDSTA, &result);
    }
    return result;
#else
#ifdef __arm__
    volatile unsigned int *p = virt_addr;
    p[0] = func_number;
    p[1] = pid;
    p[2] = (unsigned int)ret_string;
    p[3] = (unsigned int)args;
    p[4] = (unsigned int)args_size;
    p[5] = QEMUGL_HWCMD_GLCALL;
    return p[6];
#else
#error unsupported architecture!
#endif
#endif // QEMUGL_MODULE
}

static void do_init(void)
{
#ifdef QEMUGL_MODULE
    glfd = open("/dev/" QEMUGL_DEVICE_NAME, O_RDONLY);
#else
#ifdef __arm__
    void *mmap_base;
    int fd;
    off_t target=0x4fff0000;
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    mmap_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK);
    virt_addr = mmap_base + (target & MAP_MASK);
    // FIXME: hack to ensure there's only one client at a time
    volatile unsigned int *p = virt_addr;
    p[5] = 0xfeedcafe;
#else
#error unsupported architecture!
#endif
#endif // QEMUGL_MODULE
}

static int try_to_put_into_phys_memory(void *addr, int len)
{
    char c = 0;
    if (addr && len) {
        int i;
        void *aligned_addr = (void *)((long)addr & (~(pagesize - 1)));
        int to_end_page = (long)aligned_addr + pagesize - (long)addr;
        if (aligned_addr != addr) {
            c += ((char *)addr)[0];
            if (len <= to_end_page) {
                return c;
            }
            len -= to_end_page;
            addr = aligned_addr + pagesize;
        }
        for (i = 0; i < len; i += pagesize) {
            c += ((char *)addr)[0];
            addr += pagesize;
        }
    }
    return c;
}

#define RET_STRING_SIZE 32768
#define MAX_NB_ARGS 30

#define POINTER_TO_ARG(x)        (long)(void *)(x)
#define CHAR_TO_ARG(x)           (long)(x)
#define SHORT_TO_ARG(x)          (long)(x)
#define INT_TO_ARG(x)            (long)(x)
#define UNSIGNED_CHAR_TO_ARG(x)  (long)(x)
#define UNSIGNED_SHORT_TO_ARG(x) (long)(x)
#define UNSIGNED_INT_TO_ARG(x)   (long)(x)
#define FLOAT_TO_ARG(x)          (long)*((int *)(&x))
#define DOUBLE_TO_ARG(x)         (long)(&x)

static Bool glXMakeCurrent_no_lock( Display *dpy, GLXDrawable drawable, GLXContext ctx);
static void glXSwapBuffers_no_lock( Display *dpy, GLXDrawable drawable );

static void glGetIntegerv_no_lock( GLenum pname, GLint *params );
static void glReadPixels_no_lock  ( GLint x, GLint y,
                                    GLsizei width, GLsizei height,
                                    GLenum format, GLenum type,
                                    GLvoid *pixels );

static __GLXextFuncPtr glXGetProcAddress_no_lock(const char * name);

static void display_gl_call(int func_number, long* args, int* args_size)
{
    int i;
    if (func_number < 0) {
        log_gl("unknown call: %d\n", func_number);
        return;
    }
    Signature *signature = (Signature *)tab_opengl_calls[func_number];
    int nb_args = signature->nb_args;
    int *args_type = signature->args_type;
    
    static char text_[512];
    char *text = text_;
    
    text += sprintf(text_, "%s(", tab_opengl_calls_name[func_number]);
    
    for (i = 0; i < nb_args; i++) {
        switch (args_type[i]) {
            case TYPE_UNSIGNED_CHAR:
            case TYPE_CHAR:
                text += sprintf(text, "%d", (char)args[i]);
                break;
            case TYPE_UNSIGNED_SHORT:
            case TYPE_SHORT:
                text += sprintf(text, "%d", (short)args[i]);
                break;
            case TYPE_UNSIGNED_INT:
            case TYPE_INT:
                text += sprintf(text, "%d", (int)args[i]);
                break;
            case TYPE_FLOAT:
                text += sprintf(text, "%f", *(float *)&args[i]);
                break;
            case TYPE_DOUBLE:
            CASE_IN_KNOWN_SIZE_POINTERS:
            case TYPE_NULL_TERMINATED_STRING:
            CASE_IN_UNKNOWN_SIZE_POINTERS:
            CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
                if (args_type[i] == TYPE_NULL_TERMINATED_STRING) {
                    text += sprintf(text, "\"%s\"", (char *)args[i]);
                } else if (args_type[i] == TYPE_DOUBLE) {
                    text += sprintf(text, "%lf", *(double *)args[i]);
                } else if (IS_ARRAY_CHAR(args_type[i]) &&
                           args_size[i] <= 4 * sizeof(char)) {
                    int j;
                    int n = args_size[i] / sizeof(char);
                    text += sprintf(text, "(");
                    for (j = 0; j < n; j++) {
                        text += sprintf(text, "%d",
                                        ((unsigned char *)args[i])[j]);
                        if (j != n - 1) {
                            text += sprintf(text, ", ");
                        }
                    }
                    text += sprintf(text, ")");
                } else if (IS_ARRAY_SHORT(args_type[i]) &&
                           args_size[i] <= 4 * sizeof(short)) {
                    int j;
                    int n = args_size[i] / sizeof(short);
                    text += sprintf(text, "(");
                    for (j = 0; j < n; j++) {
                        text += sprintf(text, "%d", ((short *)args[i])[j]);
                        if (j != n - 1) {
                            text += sprintf(text, ", ");
                        }
                    }
                    text += sprintf(text, ")");
                } else if (IS_ARRAY_INT(args_type[i]) &&
                           args_size[i] <= 4 * sizeof(int)) {
                    int j;
                    int n = args_size[i] / sizeof(int);
                    text += sprintf(text, "(");
                    for (j = 0; j < n; j++) {
                        text += sprintf(text, "%d", ((int *)args[i])[j]);
                        if (j != n - 1) {
                            text += sprintf(text, ", ");
                        }
                    }
                    text += sprintf(text, ")");
                } else if (IS_ARRAY_FLOAT(args_type[i])
                           && args_size[i] <= 4 * sizeof(float)) {
                    int j;
                    int n = args_size[i] / sizeof(float);
                    text += sprintf(text, "(");
                    for (j = 0; j < n; j++) {
                        text += sprintf(text, "%f", ((float *)args[i])[j]);
                        if (j != n - 1) {
                            text += sprintf(text, ", ");
                        }
                    }
                    text += sprintf(text, ")");
                } else if (IS_ARRAY_DOUBLE(args_type[i]) &&
                           args_size[i] <= 4 * sizeof(double)) {
                    int j;
                    int n = args_size[i] / sizeof(double);
                    text += sprintf(text, "(");
                    for (j = 0; j < n; j++) {
                        text += sprintf(text, "%f", ((double *)args[i])[j]);
                        if (j != n - 1) {
                            text += sprintf(text, ", ");
                        }
                    }
                    text += sprintf(text, ")");
                } else {
                    text += sprintf(text, "%d bytes", args_size[i]);
                }
                break;
            CASE_OUT_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
            CASE_OUT_UNKNOWN_SIZE_POINTERS:
            CASE_OUT_KNOWN_SIZE_POINTERS:
                text += sprintf(text, "%d bytes (OUT)", args_size[i]);
                break;
            case TYPE_IN_IGNORED_POINTER:
                break;
            default:
                break;
        }
        if (i < nb_args - 1) {
            text += sprintf(text, ", ");
        }
    }
    text += sprintf(text, ")\n");
    log_gl(text_);
}

/* Must only be called if the global lock has already been taken ! */
static void do_opengl_call_no_lock(int func_number, void *ret_ptr,
                                   long *args, int *args_size_opt)
{
    if (!(func_number >= 0 && func_number < GL_N_CALLS)) {
        log_gl("func_number >= 0 && func_number < GL_N_CALLS failed\n");
        return;
    }
    
    Signature *signature = (Signature *)tab_opengl_calls[func_number];
    int nb_args = signature->nb_args;
    if (nb_args > MAX_NB_ARGS) {
        log_gl("\"%s\": nb_args(%d) > MAX_NB_ARGS(%d)\n",
               tab_opengl_calls_name[func_number], nb_args, MAX_NB_ARGS);
        return;
    }
    
    int *args_type = signature->args_type;
    int args_size[MAX_NB_ARGS];
    int ret_int = 0;
    int i;
    
    static char *ret_string = NULL;
    static int last_current_thread = -1;
    static int exists_on_server_side[GL_N_CALLS];
    
    int current_thread = GET_CURRENT_THREAD();
    
    if (ret_string == NULL) {
        /* Sanity checks */
        assert(tab_args_type_length[TYPE_OUT_128UCHAR] == 128 * sizeof(char));
        assert(tab_args_type_length[TYPE_OUT_ARRAY_DOUBLE_OF_LENGTH_DEPENDING_ON_PREVIOUS_ARGS] == sizeof(double));
        assert(sizeof(tab_args_type_length)/sizeof(tab_args_type_length[0]) == TYPE_LAST);
        
        memset(exists_on_server_side, 255, sizeof(exists_on_server_side));
        exists_on_server_side[glXGetProcAddress_fake_func] = 1;
        exists_on_server_side[glXGetProcAddress_global_fake_func] = 1;
        
        last_current_thread = current_thread;
        
        do_init();
        
        pagesize = getpagesize();
        debug_gl = getenv("DEBUG_GL") != NULL;
        
        posix_memalign((void **)&ret_string, pagesize, RET_STRING_SIZE);
        memset(ret_string, 0, RET_STRING_SIZE);
        mlock(ret_string, RET_STRING_SIZE);
        
        int result = call_opengl(_init_func, getpid(), NULL, NULL, NULL);
        if (result != 0x51) {
            log_gl("Unable to initialize QEMU virtual OpenGL device (code 0x%x)\nexiting...\n",
                   result);
            exit(-1);
        }
    }
    
    if (exists_on_server_side[func_number] == -1) {
        if (strchr(tab_opengl_calls_name[func_number], '_')) {
            exists_on_server_side[func_number] = 1;
        } else {
            exists_on_server_side[func_number] = glXGetProcAddress_no_lock(tab_opengl_calls_name[func_number]) != NULL;
        }
        if (exists_on_server_side[func_number] == 0) {
            log_gl("oops: symbol \"%s\" not available in QEMU virtual OpenGL device\n",
                   tab_opengl_calls_name[func_number]);
            return;
        }
    } else if (exists_on_server_side[func_number] == 0) {
        return;
    }
    
    GET_CURRENT_STATE();
    
#ifdef ENABLE_THREAD_SAFETY
    if (last_current_thread != current_thread) {
        last_current_thread = current_thread;
        DEBUGLOG_GL("gl thread switch\n");
        glXMakeCurrent_no_lock(state->display, state->current_drawable, state->context);
    }
#endif
    
    for (i = 0; i < nb_args; i++) {
        switch (args_type[i]) {
            case TYPE_UNSIGNED_INT:
            case TYPE_INT:
            case TYPE_UNSIGNED_CHAR:
            case TYPE_CHAR:
            case TYPE_UNSIGNED_SHORT:
            case TYPE_SHORT:
            case TYPE_FLOAT:
                args_size[i] = tab_args_type_length[args_type[i]];
                break;
            CASE_IN_UNKNOWN_SIZE_POINTERS:
                args_size[i] = args_size_opt[i];
                if (args_size[i] < 0) {
                    log_gl("size < 0 : func=%s, param=%d\n", tab_opengl_calls_name[func_number], i);
                    return;
                }
                try_to_put_into_phys_memory((void *)args[i], args_size[i]);
                break;
            case TYPE_NULL_TERMINATED_STRING:
                args_size[i] = strlen((const char *)args[i]) + 1;
                break;
            CASE_IN_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
            CASE_OUT_LENGTH_DEPENDING_ON_PREVIOUS_ARGS:
                args_size[i] = compute_arg_length(get_err_file(), func_number, i, (unsigned long *)args);
                break;
            case TYPE_IN_IGNORED_POINTER:
                args_size[i] = 0;
                break;
            CASE_OUT_UNKNOWN_SIZE_POINTERS:
                args_size[i] = args_size_opt[i];
                if (args_size[i] < 0) {
                    log_gl("size < 0 : func=%s, param=%d\n", tab_opengl_calls_name[func_number], i);
                    return;
                }
                try_to_put_into_phys_memory((void *)args[i], args_size[i]);
                break;
            case TYPE_DOUBLE:
            CASE_KNOWN_SIZE_POINTERS:
                args_size[i] = tab_args_type_length[args_type[i]];
                try_to_put_into_phys_memory((void *)args[i], args_size[i]);
                break;
            default:
                log_gl("unexpected arg type %d at i=%d\n", args_type[i], i);
                exit(-1);
                break;
        }
    }
    
    if (debug_gl) {
        display_gl_call(func_number, args, args_size);
    }
    
    if (signature->ret_type == TYPE_CONST_CHAR) {
        try_to_put_into_phys_memory(ret_string, RET_STRING_SIZE);
    }
    ret_int = call_opengl(func_number, getpid(),
                          (signature->ret_type == TYPE_CONST_CHAR)
                          ? ret_string : NULL,
                          args, args_size);
    
    switch (signature->ret_type) {
        case TYPE_UNSIGNED_INT:
        case TYPE_INT:
            *(int *)ret_ptr = ret_int;
            break;
        case TYPE_UNSIGNED_CHAR:
        case TYPE_CHAR:
            *(char *)ret_ptr = ret_int;
            break;
        case TYPE_CONST_CHAR:
            *(char **)(ret_ptr) = ret_string;
            break;
        default:
            break;
    }
}

static inline void do_opengl_call(int func_number, void *ret_ptr,
                                  long *args, int *args_size_opt)
{
    LOCK(func_number);
    do_opengl_call_no_lock(func_number, ret_ptr, args, args_size_opt);
    UNLOCK(func_number);
}

#include "client_stub.c"

static inline int datatypesize_gl(int type)
{
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_2_BYTES:
            return 2;
        case GL_3_BYTES:
            return 3;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_4_BYTES:
        case GL_UNSIGNED_INT_24_8_EXT:
            return 4;
        case GL_DOUBLE:
            return 8;
        default:
            break;
    }
    log_gl("%s: unsupported type 0x%x\n", __FUNCTION__, type);
    return 0;
}


static int get_nb_composants_of_gl_get_constant_compare(const void *a,
                                                        const void *b)
{
    GlGetConstant *constantA = (GlGetConstant *)a;
    GlGetConstant *constantB = (GlGetConstant *)b;
    return constantA->token - constantB->token;
}

static int get_size_get_boolean_integer_float_double_v(int func_number,
                                                       int pname)
{
    GlGetConstant constant;
    GlGetConstant *found;
    constant.token = pname;
    found = bsearch(&constant, gl_get_constants,
                    sizeof(gl_get_constants) / sizeof(GlGetConstant),
                    sizeof(GlGetConstant),
                    get_nb_composants_of_gl_get_constant_compare);
    if (found) {
        return found->count;
    }
    log_gl("unknown name for %s : %d\nhoping that size is 1...\n",
           tab_opengl_calls_name[func_number], pname);
    return 1;
}

#define glGetv(funcName, cType, typeBase) \
static void CONCAT(funcName,_no_lock)(GLenum pname, cType *params) \
{ \
    long args[] = {INT_TO_ARG(pname), POINTER_TO_ARG(params)}; \
    int args_size[] = {0, 0}; \
    args_size[1] = tab_args_type_length[typeBase] * \
        get_size_get_boolean_integer_float_double_v(FUNC_NUM(funcName), \
                                                    pname); \
    do_opengl_call_no_lock(FUNC_NUM(funcName), NULL, \
                           CHECK_ARGS(args, args_size)); \
} \
GLAPI void APIENTRY funcName(GLenum pname, cType *params) \
{ \
    LOCK(FUNC_NUM(funcName)); \
    CONCAT(funcName,_no_lock)(pname, params); \
    UNLOCK(FUNC_NUM(funcName)); \
}

glGetv(glGetBooleanv, GLboolean, TYPE_CHAR);
glGetv(glGetIntegerv, GLint, TYPE_INT);
glGetv(glGetFloatv, GLfloat, TYPE_FLOAT);
glGetv(glGetDoublev, GLdouble, TYPE_DOUBLE);

GLAPI void APIENTRY glViewport(GLint x, GLint y,
                               GLsizei width, GLsizei height)
{
    GET_CURRENT_STATE();
    long args[] = {INT_TO_ARG(x), INT_TO_ARG(y),
                   INT_TO_ARG(width), INT_TO_ARG(height)};
    state->viewport.x = x;
    state->viewport.y = y;
    state->viewport.width = width;
    state->viewport.height = height;
    DEBUGLOG_GL("viewport %d,%d,%d,%d\n", x, y, width, height);
    do_opengl_call(glViewport_func, NULL, args, NULL);
}

#define glGenBuffers_(funcname) \
GLAPI void APIENTRY funcname(GLsizei n, GLuint *tab) \
{ \
    long args[] = {INT_TO_ARG(n), POINTER_TO_ARG(tab)}; \
    int args_size[] = {0, n * sizeof(GLuint)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGenBuffers_(glGenBuffers);
glGenBuffers_(glGenBuffersARB);

#define glBufferData_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLsizeiptrARB size, \
                             const GLvoid *data, GLenum usage) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(size), \
                   POINTER_TO_ARG(data), INT_TO_ARG(usage)}; \
    int args_size[] = { 0, 0, (data) ? size : 0, 0 }; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glBufferData_(glBufferData);
glBufferData_(glBufferDataARB);

#define glBufferSubData_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLintptrARB offset, \
                             GLsizeiptrARB size, const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(offset), INT_TO_ARG(size), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = { 0, 0, 0, size }; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glBufferSubData_(glBufferSubData);
glBufferSubData_(glBufferSubDataARB);

#define glGetBufferSubData_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLintptrARB offset, \
                             GLsizeiptrARB size, GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(offset), INT_TO_ARG(size), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGetBufferSubData_(glGetBufferSubData);
glGetBufferSubData_(glGetBufferSubDataARB);

#define glMapBuffer_(funcname) \
GLAPI GLvoid * APIENTRY funcname(GLenum target, GLenum access) \
{ \
    NOT_IMPLEMENTED(); \
    return NULL; \
} 

glMapBuffer_(glMapBuffer);
glMapBuffer_(glMapBufferARB);

#define glGetBufferPointerv_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum pname, GLvoid **params) \
{ \
    NOT_IMPLEMENTED(); \
    *params = NULL; \
}

glGetBufferPointerv_(glGetBufferPointerv);
glGetBufferPointerv_(glGetBufferPointervARB);

#define glUnmapBuffer_(funcname) \
GLAPI GLboolean APIENTRY funcname(GLenum target) \
{ \
    NOT_IMPLEMENTED(); \
    return 1; \
}

glUnmapBuffer_(glUnmapBuffer);
glUnmapBuffer_(glUnmapBufferARB);

GLAPI void APIENTRY glCallLists(GLsizei n,
                                GLenum type,
                                const GLvoid *lists)
{
    long args[] = {INT_TO_ARG(n), INT_TO_ARG(type), POINTER_TO_ARG(lists)};
    int args_size[] = {0, 0, n * datatypesize_gl(type)};
    if (n <= 0) {
        log_gl("n <= 0\n");
        return;
    }
    do_opengl_call(glCallLists_func, NULL, CHECK_ARGS(args, args_size));
}

GLAPI const GLubyte * APIENTRY glGetString(GLenum name)
{
    int i;
    static GLubyte *glStrings[6] = {NULL};
    
    if (name >= GL_VENDOR && name <= GL_EXTENSIONS) {
        i = name - GL_VENDOR;
    } else if (name == GL_SHADING_LANGUAGE_VERSION) {
        i = 4;
    } else if (name == GL_PROGRAM_ERROR_STRING_NV) {
        i = 5;
    } else {
        log_gl("%s: unknown name 0x%x\n", __FUNCTION__, name);
        return NULL;
    }
    LOCK(glGetString_func);
    if (glStrings[i] == NULL) {
        long args[] = {INT_TO_ARG(name)};
        do_opengl_call_no_lock(glGetString_func, &glStrings[i], args, NULL);
        DEBUGLOG_GL("glGetString(0x%X) = %s\n", name, glStrings[i]);
        glStrings[name - GL_VENDOR] = (GLubyte *)strdup((char *)glStrings[i]);
    }
    UNLOCK(glGetString_func);
    return glStrings[i];
}

#define CASE_GL_PIXEL_MAP(x) \
    case GL_PIXEL_MAP_##x: \
        glGetIntegerv(CONCAT(GL_PIXEL_MAP_##x,_SIZE), &value); \
        return value;

static int get_glgetpixelmapv_size(int map)
{
    int value;
    switch (map) {
        CASE_GL_PIXEL_MAP(I_TO_I);
        CASE_GL_PIXEL_MAP(S_TO_S);
        CASE_GL_PIXEL_MAP(I_TO_R);
        CASE_GL_PIXEL_MAP(I_TO_G);
        CASE_GL_PIXEL_MAP(I_TO_B);
        CASE_GL_PIXEL_MAP(I_TO_A);
        CASE_GL_PIXEL_MAP(R_TO_R);
        CASE_GL_PIXEL_MAP(G_TO_G);
        CASE_GL_PIXEL_MAP(B_TO_B);
        CASE_GL_PIXEL_MAP(A_TO_A);
        default:
            log_gl("%s: unhandled map = %d\n", __FUNCTION__, map);
            return 0;
    }
}

#define glGetPixelMapv_(funcname, type) \
GLAPI void APIENTRY funcname(GLenum map, type *values) \
{ \
    long args[] = {INT_TO_ARG(map), POINTER_TO_ARG(values)}; \
    int args_size[] = {0, get_glgetpixelmapv_size(map) * sizeof(type)}; \
    if (args_size[1]) { \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glGetPixelMapv_(glGetPixelMapfv, GLfloat);
glGetPixelMapv_(glGetPixelMapuiv, GLuint);
glGetPixelMapv_(glGetPixelMapusv, GLushort);

static int glMap1_get_multiplier(GLenum target)
{
    switch (target) {
        case GL_MAP1_VERTEX_3:
        case GL_MAP1_NORMAL:
        case GL_MAP1_TEXTURE_COORD_3:
            return 3;
        case GL_MAP1_VERTEX_4:
        case GL_MAP1_COLOR_4:
        case GL_MAP1_TEXTURE_COORD_4:
            return 4;
        case GL_MAP1_INDEX:
        case GL_MAP1_TEXTURE_COORD_1:
            return 1;
        case GL_MAP1_TEXTURE_COORD_2:
            return 2;
        default:
            if (target >= GL_MAP1_VERTEX_ATTRIB0_4_NV &&
                target <= GL_MAP1_VERTEX_ATTRIB15_4_NV) {
                return 4;
            }
            break;
    }
    log_gl("%s: unhandled target = %d\n", __FUNCTION__, target);
    return 0;
}

static int glMap2_get_multiplier(GLenum target)
{
    switch (target) {
        case GL_MAP2_VERTEX_3:
        case GL_MAP2_NORMAL:
        case GL_MAP2_TEXTURE_COORD_3:
            return 3;
        case GL_MAP2_VERTEX_4:
        case GL_MAP2_COLOR_4:
        case GL_MAP2_TEXTURE_COORD_4:
            return 4;
        case GL_MAP2_INDEX:
        case GL_MAP2_TEXTURE_COORD_1:
            return 1;
        case GL_MAP2_TEXTURE_COORD_2:
            return 2;
        default:
            if (target >= GL_MAP2_VERTEX_ATTRIB0_4_NV &&
                target <= GL_MAP2_VERTEX_ATTRIB15_4_NV) {
                return 4;
            }
            break;
    }
    log_gl("%s: unhandled target = %d\n", __FUNCTION__, target);
    return 0;
}

static int get_dimensional_evaluator(GLenum target)
{
    switch (target) {
        case GL_MAP1_COLOR_4:
        case GL_MAP1_INDEX:
        case GL_MAP1_NORMAL:
        case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP1_TEXTURE_COORD_2:
        case GL_MAP1_TEXTURE_COORD_3:
        case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP1_VERTEX_3:
        case GL_MAP1_VERTEX_4:
            return 1;
        case GL_MAP2_COLOR_4:
        case GL_MAP2_INDEX:
        case GL_MAP2_NORMAL:
        case GL_MAP2_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_2:
        case GL_MAP2_TEXTURE_COORD_3:
        case GL_MAP2_TEXTURE_COORD_4:
        case GL_MAP2_VERTEX_3:
        case GL_MAP2_VERTEX_4:
            return 2;
        default:
            break;
    }
    log_gl("%s: unhandled target %d\n", __FUNCTION__, target);
    return 0;
}

GLAPI void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2,
                            GLint stride, GLint order, const GLfloat *points)
{
    int multiplier = glMap1_get_multiplier(target);
    if (multiplier) {
        int num_points = order * multiplier;
        long args[] = {INT_TO_ARG(target), FLOAT_TO_ARG(u1), FLOAT_TO_ARG(u2),
                       INT_TO_ARG(stride), INT_TO_ARG(order),
                       POINTER_TO_ARG(points)};
        int args_size[] = {0, 0, 0, 0, 0, num_points * sizeof(GLfloat)};
        do_opengl_call(glMap1f_func, NULL, CHECK_ARGS(args, args_size));
    }
}

GLAPI void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2,
                            GLint stride, GLint order, const GLdouble *points)
{
    int multiplier = glMap1_get_multiplier(target);
    if (multiplier) {
        int num_points = order * multiplier;
        long args[] = {INT_TO_ARG(target), DOUBLE_TO_ARG(u1),
                       DOUBLE_TO_ARG(u2), INT_TO_ARG(stride),
                       INT_TO_ARG(order), POINTER_TO_ARG(points)};
        int args_size[] = {0, 0, 0, 0, 0, num_points * sizeof(GLdouble)};
        do_opengl_call(glMap1d_func, NULL, CHECK_ARGS(args, args_size));
    }
}

GLAPI void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2,
                            GLint ustride, GLint uorder, GLfloat v1,
                            GLfloat v2, GLint vstride, GLint vorder,
                            const GLfloat *points)
{
    int multiplier = glMap2_get_multiplier(target);
    if (multiplier) {
        int num_points = uorder * vorder * multiplier;
        long args[] = {INT_TO_ARG(target), FLOAT_TO_ARG(u1),
                       FLOAT_TO_ARG(u2), INT_TO_ARG(ustride),
                       INT_TO_ARG(uorder), FLOAT_TO_ARG(v1),
                       FLOAT_TO_ARG(v2), INT_TO_ARG(vstride),
                       INT_TO_ARG(vorder), POINTER_TO_ARG(points)};
        int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, 0,
                           num_points * sizeof(GLfloat)};
        do_opengl_call(glMap2f_func, NULL, CHECK_ARGS(args, args_size));
    }
}

GLAPI void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2,
                            GLint ustride, GLint uorder, GLdouble v1,
                            GLdouble v2, GLint vstride, GLint vorder,
                            const GLdouble *points)
{
    int multiplier = glMap2_get_multiplier(target);
    if (multiplier) {
        int num_points = uorder * vorder * multiplier;
        long args[] = {INT_TO_ARG(target), DOUBLE_TO_ARG(u1),
                       DOUBLE_TO_ARG(u2), INT_TO_ARG(ustride),
                       INT_TO_ARG(uorder), DOUBLE_TO_ARG(v1),
                       DOUBLE_TO_ARG(v2), INT_TO_ARG(vstride),
                       INT_TO_ARG(vorder), POINTER_TO_ARG(points)};
        int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 
                           num_points * sizeof(GLdouble)};
        do_opengl_call(glMap2d_func, NULL, CHECK_ARGS(args, args_size));
    }
}

static int glGetMapv_get_n_components(GLenum target, GLenum query)
{
    int dim = get_dimensional_evaluator(target);
    if (query == GL_COEFF) {
        int orders[2] = {1, 1};
        glGetMapiv(target, GL_ORDER, orders);
        return orders[0] * orders[1] *
            ((dim == 1) 
                ? glMap1_get_multiplier(target) 
                : glMap2_get_multiplier(target));
    } else if (query == GL_ORDER) {
        return dim;
    } else if (query == GL_DOMAIN) {
        return 2 * dim;
    }
    return 0;
}

#define glGetMapv_(funcname, type) \
GLAPI void APIENTRY funcname(GLenum target, GLenum query, type *v) \
{ \
    if (get_dimensional_evaluator(target)) { \
        long args[] = {INT_TO_ARG(target), INT_TO_ARG(query), \
                       POINTER_TO_ARG(v)}; \
        int args_size[] = {0, 0, \
            glGetMapv_get_n_components(target, query) * sizeof(type)}; \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glGetMapv_(glGetMapdv, GLdouble);
glGetMapv_(glGetMapfv, GLfloat);
glGetMapv_(glGetMapiv, GLint);

#define glGenTextures_(funcname) \
GLAPI void APIENTRY funcname(GLsizei n, GLuint *textures) \
{ \
    long args[] = {INT_TO_ARG(n), POINTER_TO_ARG(textures)}; \
    int args_size[] = {0, n * sizeof(GLuint)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGenTextures_(glGenTextures);
glGenTextures_(glGenTexturesEXT);

static int getTexImageFactorFromFormatAndType(int format, int type)
{
    switch (format) {
        case GL_COLOR_INDEX:
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
        case GL_INTENSITY:
        case GL_DEPTH_COMPONENT:
        case GL_STENCIL_INDEX:
        case GL_DEPTH_STENCIL_EXT:
            return 1 * datatypesize_gl(type);
        case GL_LUMINANCE_ALPHA:
            return 2 * datatypesize_gl(type);
        case GL_YCBCR_MESA:
            switch (type) {
                case GL_UNSIGNED_SHORT_8_8_MESA:
                case GL_UNSIGNED_SHORT_8_8_REV_MESA:
                    return 2;
                default:
                    break;
            }
            break;
        case GL_RGB:
        case GL_BGR:
            switch (type) {
                case GL_UNSIGNED_BYTE:
                case GL_BYTE:
                case GL_UNSIGNED_SHORT:
                case GL_SHORT:
                case GL_UNSIGNED_INT:
                case GL_INT:
                case GL_FLOAT:
                    return 3 * datatypesize_gl(type);
                case GL_UNSIGNED_BYTE_3_3_2:
                case GL_UNSIGNED_BYTE_2_3_3_REV:
                    return 1;
                case GL_UNSIGNED_SHORT_5_6_5:
                case GL_UNSIGNED_SHORT_5_6_5_REV:
                case GL_UNSIGNED_SHORT_8_8_MESA:
                case GL_UNSIGNED_SHORT_8_8_REV_MESA:
                    return 2;
                default:
                    break;
            }
            break;
        case GL_RGBA:
        case GL_BGRA:
        case GL_ABGR_EXT:
            switch (type) {
                case GL_UNSIGNED_BYTE:
                case GL_BYTE:
                case GL_UNSIGNED_SHORT:
                case GL_SHORT:
                case GL_UNSIGNED_INT:
                case GL_INT:
                case GL_FLOAT:
                    return 4 * datatypesize_gl(type);
                case GL_UNSIGNED_SHORT_4_4_4_4:
                case GL_UNSIGNED_SHORT_4_4_4_4_REV:
                case GL_UNSIGNED_SHORT_5_5_5_1:
                case GL_UNSIGNED_SHORT_1_5_5_5_REV:
                    return 2;
                case GL_UNSIGNED_INT_8_8_8_8:
                case GL_UNSIGNED_INT_8_8_8_8_REV:
                case GL_UNSIGNED_INT_10_10_10_2:
                case GL_UNSIGNED_INT_2_10_10_10_REV:
                    return 4;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    log_gl("unknown texture type 0x%x or format 0x%x\n", type, format);
    return 0;
}

static void *calc_readsize(int width, int height, int depth, GLenum format,
                           GLenum type, const void *pixels, int *p_size)
{
    int pack_row_length, pack_alignment, pack_skip_rows, pack_skip_pixels;
    
    glGetIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &pack_skip_rows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &pack_skip_pixels);
    
    int w = (pack_row_length == 0) ? width : pack_row_length;
    int size = ((width * getTexImageFactorFromFormatAndType(format, type)
                 + pack_alignment - 1) & (~(pack_alignment - 1))) * depth;
    if (height >= 1) {
        size += ((w * getTexImageFactorFromFormatAndType(format, type)
                  + pack_alignment - 1) & (~(pack_alignment - 1)))
                * (height - 1) * depth;
    }
    *p_size = size;
    
    pixels += (pack_skip_pixels + pack_skip_rows * w)
              * getTexImageFactorFromFormatAndType(format, type);
    
    return (void *)pixels;
}

static const void *calc_writesize(int width, int height, int depth,
                                  GLenum format, GLenum type,
                                  const void *pixels, int* p_size)
{
    int unpack_row_length, unpack_alignment;
    int unpack_skip_rows, unpack_skip_pixels;
    
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels);
    
    int w = (unpack_row_length == 0) ? width : unpack_row_length;
    int size = ((width * getTexImageFactorFromFormatAndType(format, type)
                 + unpack_alignment - 1) & (~(unpack_alignment - 1))) * depth;
    if (height >= 1) {
        size += ((w * getTexImageFactorFromFormatAndType(format, type)
                  + unpack_alignment - 1) & (~(unpack_alignment - 1)))
                * (height - 1) * depth;
    }
    *p_size = size;
    
    pixels += (unpack_skip_pixels + unpack_skip_rows * w)
              * getTexImageFactorFromFormatAndType(format, type);
    
    return pixels;
}

#define glTexImage1D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, \
                             GLint internalFormat, GLsizei width, \
                             GLint border, GLenum format, GLenum type, \
                             const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, 1, 1, format, type, pixels, &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(border), INT_TO_ARG(format), INT_TO_ARG(type), \
                   POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, (pixels == NULL) ? 0 : size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexImage1D_(glTexImage1D);

#define glTexImage2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level,\
                             GLint internalFormat, GLsizei width, \
                             GLsizei height, GLint border, GLenum format, \
                             GLenum type, const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, height, 1, format, type, pixels, \
                                &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(height), INT_TO_ARG(border), \
                   INT_TO_ARG(format), INT_TO_ARG(type), \
                   POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, (pixels == NULL) ? 0 : size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexImage2D_(glTexImage2D);

#define glTexImage3D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, \
                             GLint internalFormat, GLsizei width, \
                             GLsizei height, GLsizei depth, GLint border, \
                             GLenum format, GLenum type, \
                             const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, height, depth, format, type, pixels, &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(height), INT_TO_ARG(depth), INT_TO_ARG(border), \
                   INT_TO_ARG(format), INT_TO_ARG(type), \
                   POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, pixels ? size: 0}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexImage3D_(glTexImage3D);
glTexImage3D_(glTexImage3DEXT);

#define glTexSubImage1D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLsizei width, GLenum format, GLenum type, \
                             const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, 1, 1, format, type, pixels, &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(xoffset), INT_TO_ARG(width), \
                   INT_TO_ARG(format), INT_TO_ARG(type), \
                   POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexSubImage1D_(glTexSubImage1D);
glTexSubImage1D_(glTexSubImage1DEXT);

#define glTexSubImage2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLint yoffset, GLsizei width, GLsizei height, \
                             GLenum format, GLenum type, \
                             const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, height, 1, format, type, pixels, \
                                &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(xoffset), INT_TO_ARG(yoffset), \
                   INT_TO_ARG(width), INT_TO_ARG(height), INT_TO_ARG(format), \
                   INT_TO_ARG(type), POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexSubImage2D_(glTexSubImage2D);
glTexSubImage2D_(glTexSubImage2DEXT);

#define glTexSubImage3D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLint yoffset, GLint zoffset, GLsizei width, \
                             GLsizei height, GLsizei depth, GLenum format, \
                             GLenum type, const GLvoid *pixels) \
{ \
    int size = 0; \
    if (pixels) { \
        pixels = calc_writesize(width, height, depth, format, type, pixels, \
                                &size); \
    } \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(xoffset), INT_TO_ARG(yoffset), \
                   INT_TO_ARG(zoffset), INT_TO_ARG(width), \
                   INT_TO_ARG(height), INT_TO_ARG(depth), \
                   INT_TO_ARG(format), INT_TO_ARG(type), \
                   POINTER_TO_ARG(pixels)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, size}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glTexSubImage3D_(glTexSubImage3D);
glTexSubImage3D_(glTexSubImage3DEXT);

GLAPI void APIENTRY glSelectBuffer(GLsizei size, GLuint *buffer)
{
    if (size > 0) {
        long args[] = {INT_TO_ARG(size), POINTER_TO_ARG(buffer)};
        int args_size[] = {0, size * sizeof(GLuint)};
        do_opengl_call(glSelectBuffer_func, NULL, args, args_size);
    }
}

GLAPI void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type,
                                     GLfloat *buffer)
{
    if (size > 0) {
        long args[] = {INT_TO_ARG(size), INT_TO_ARG(type),
                       POINTER_TO_ARG(buffer)};
        int args_size[] = {0, 0, sizeof(GLfloat)};
        // FIXME: in color index modes the size is 3 bytes less!
        switch (type) {
            case GL_2D:
                args[2] *= 2;
                break;
            case GL_3D:
                args[2] *= 3;
                break;
            case GL_3D_COLOR:
                args[2] = args[2] * 3 + 4; /* s/4/1/ in color index mode */
                break;
            case GL_3D_COLOR_TEXTURE:
                args[2] = args[2] * 7 + 4; /* s/4/1/ in color index mode */
                break;
            case GL_4D_COLOR_TEXTURE:
                args[2] = args[2] * 8 + 4; /* s/4/1/ in color index mode */
                break;
            default:
                log_gl("%s: unknown type %d\n", __FUNCTION__, type);
                break;
        }
        do_opengl_call(glFeedbackBuffer_func, NULL, args, args_size);
    }
}

#define glGetCompressedTexImage_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLvoid *img) \
{ \
    int imageSize = 0; \
    glGetTexLevelParameteriv(target, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, \
                             &imageSize); \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   POINTER_TO_ARG(img)}; \
    int args_size[] = {0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGetCompressedTexImage_(glGetCompressedTexImage);
glGetCompressedTexImage_(glGetCompressedTexImageARB);

#define glCompressedTexImage1D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, \
                             GLenum internalFormat, GLsizei width, \
                             GLint border, GLsizei imageSize, \
                             const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(border), INT_TO_ARG(imageSize), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexImage1D_(glCompressedTexImage1D);
glCompressedTexImage1D_(glCompressedTexImage1DARB);

#define glCompressedTexImage2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, \
                             GLenum internalFormat, GLsizei width, \
                             GLsizei height, GLint border, \
                             GLsizei imageSize, const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(height), INT_TO_ARG(border), \
                   INT_TO_ARG(imageSize), POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexImage2D_(glCompressedTexImage2D);
glCompressedTexImage2D_(glCompressedTexImage2DARB);

#define glCompressedTexImage3D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, \
                             GLenum internalFormat, GLsizei width, \
                             GLsizei height, GLsizei depth, GLint border, \
                             GLsizei imageSize, const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(internalFormat), INT_TO_ARG(width), \
                   INT_TO_ARG(height), INT_TO_ARG(depth), \
                   INT_TO_ARG(border), INT_TO_ARG(imageSize), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexImage3D_(glCompressedTexImage3D);
glCompressedTexImage3D_(glCompressedTexImage3DARB);

#define glCompressedTexSubImage1D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLsizei width, GLenum format, GLsizei imageSize, \
                             const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), \
                   INT_TO_ARG(xoffset), INT_TO_ARG(width), INT_TO_ARG(format), \
                   INT_TO_ARG(imageSize), POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexSubImage1D_(glCompressedTexSubImage1D);
glCompressedTexSubImage1D_(glCompressedTexSubImage1DARB);

#define glCompressedTexSubImage2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLint yoffset, GLsizei width, GLsizei height, \
                             GLenum format, GLsizei imageSize, \
                             const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), INT_TO_ARG(xoffset), \
                   INT_TO_ARG(yoffset), INT_TO_ARG(width), INT_TO_ARG(height), \
                   INT_TO_ARG(format), INT_TO_ARG(imageSize), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexSubImage2D_(glCompressedTexSubImage2D);
glCompressedTexSubImage2D_(glCompressedTexSubImage2DARB);

#define glCompressedTexSubImage3D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLint level, GLint xoffset, \
                             GLint yoffset, GLint zoffset, GLsizei width, \
                             GLsizei height, GLsizei depth, GLenum format, \
                             GLsizei imageSize, const GLvoid *data) \
{ \
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), INT_TO_ARG(xoffset), \
                   INT_TO_ARG(yoffset), INT_TO_ARG(zoffset), \
                   INT_TO_ARG(width), INT_TO_ARG(height), INT_TO_ARG(depth), \
                   INT_TO_ARG(format), INT_TO_ARG(imageSize), \
                   POINTER_TO_ARG(data)}; \
    int args_size[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, imageSize}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glCompressedTexSubImage3D_(glCompressedTexSubImage3D);
glCompressedTexSubImage3D_(glCompressedTexSubImage3DARB);

GLAPI void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname,
                                        GLfloat *params)
{
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(pname),
                   POINTER_TO_ARG(params)};
    int args_size[] = {0, 0, glTexParameter_size_(get_err_file(), pname)
                             * sizeof(GLfloat)};
    do_opengl_call(glGetTexParameterfv_func, NULL,
                   CHECK_ARGS(args, args_size));
}

GLAPI void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2)
{
    glRectd(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2)
{
    glRectf(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectiv(const GLint *v1, const GLint *v2)
{
    glRecti(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2)
{
    glRects(v1[0], v1[1], v2[0], v2[1]);
}

GLAPI void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig,
                             GLfloat yorig, GLfloat xmove, GLfloat ymove,
                             const GLubyte *bitmap)
{
    int unpack_alignment, unpack_row_length;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    int w = (unpack_row_length == 0) ? width : unpack_row_length;
    int size = ((w + unpack_alignment - 1) & (~(unpack_alignment - 1)))
               * height;
    long args[] = {INT_TO_ARG(width), INT_TO_ARG(height), FLOAT_TO_ARG(xorig),
                   FLOAT_TO_ARG(yorig), FLOAT_TO_ARG(xmove),
                   FLOAT_TO_ARG(ymove), POINTER_TO_ARG(bitmap)};
    int args_size[] = {0, 0, 0, 0, 0, 0, size};
    do_opengl_call(glBitmap_func, NULL, CHECK_ARGS(args, args_size));
}

GLAPI void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format,
                                  GLenum type, GLvoid *pixels)
{
    int size = 0, width, height = 1, depth = 1;
    if (target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_2D ||
        target == GL_PROXY_TEXTURE_3D) {
        log_gl("%s: unhandled target 0x%x\n", __FUNCTION__, target);
        return;
    }
    glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &width);
    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_RECTANGLE_ARB ||
        target == GL_TEXTURE_3D) {
        glGetTexLevelParameteriv(target, level, GL_TEXTURE_HEIGHT, &height);
    }
    if (target == GL_TEXTURE_3D) {
        glGetTexLevelParameteriv(target, level, GL_TEXTURE_DEPTH, &depth);
    }
    pixels = calc_readsize(width, height, depth, format, type, pixels, &size);
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(level), INT_TO_ARG(format),
                   INT_TO_ARG(type), POINTER_TO_ARG(pixels)};
    int args_size[] = {0, 0, 0, 0, size};
    do_opengl_call(glGetTexImage_func, NULL, CHECK_ARGS(args, args_size));
}

static void glReadPixels_no_lock(GLint x, GLint y, GLsizei width,
                                 GLsizei height, GLenum format, GLenum type,
                                 GLvoid *pixels)
{
    int size = 0;
    pixels = calc_readsize(width, height, 1, format, type, pixels, &size);
    long args[] = {INT_TO_ARG(x), INT_TO_ARG(y), INT_TO_ARG(width),
                   INT_TO_ARG(height), INT_TO_ARG(format), INT_TO_ARG(type),
                   POINTER_TO_ARG(pixels)};
    int args_size[] = {0, 0, 0, 0, 0, 0, size};
    do_opengl_call_no_lock(glReadPixels_func, NULL,
                           CHECK_ARGS(args, args_size));
}

GLAPI void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width,
                                 GLsizei height, GLenum format, GLenum type,
                                 GLvoid *pixels)
{
    LOCK(glReadPixels_func);
    glReadPixels_no_lock(x, y, width, height, format, type, pixels);
    UNLOCK(glReadPixels_func);
}

GLAPI void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format,
                                 GLenum type, const GLvoid *pixels)
{
    int size = 0;
    pixels = calc_writesize(width, height, 1, format, type, pixels, &size);
    long args[] = {INT_TO_ARG(width), INT_TO_ARG(height), INT_TO_ARG(format),
                   INT_TO_ARG(type), POINTER_TO_ARG(pixels)};
    int args_size[] = {0, 0, 0, 0, size};
    do_opengl_call(glDrawPixels_func, NULL, CHECK_ARGS(args, args_size));
}

static int calc_interleaved_arrays_stride(GLenum format)
{
    switch (format) {
        case GL_V2F:
            return 2 * sizeof(float);
        case GL_V3F:
            return 3 * sizeof(float);
        case GL_C4UB_V2F:
            return 4 * sizeof(char) + 2 * sizeof(float);
        case GL_C4UB_V3F:
            return 4 * sizeof(char) + 3 * sizeof(float);
        case GL_C3F_V3F:
            return 3 * sizeof(float) + 3 * sizeof(float);
        case GL_N3F_V3F:
            return 3 * sizeof(float) + 3 * sizeof(float);
        case GL_C4F_N3F_V3F:
            return 4 * sizeof(float) + 3 * sizeof(float) + 3 * sizeof(float);
        case GL_T2F_V3F:
            return 2 * sizeof(float) + 3 * sizeof(float);
        case GL_T4F_V4F:
            return 4 * sizeof(float) + 4 * sizeof(float);
        case GL_T2F_C4UB_V3F:
            return 2 * sizeof(float) + 4 * sizeof(char) + 3 * sizeof(float);
        case GL_T2F_C3F_V3F:
            return 2 * sizeof(float) + 3 * sizeof(float) + 3 * sizeof(float);
        case GL_T2F_N3F_V3F:
            return 2 * sizeof(float) + 3 * sizeof(float) + 3 * sizeof(float);
        case GL_T2F_C4F_N3F_V3F:
            return 2 * sizeof(float) + 4 * sizeof(float) + 3 * sizeof(float)
                   + 3 * sizeof(float);
        case GL_T4F_C4F_N3F_V4F:
            return 4 * sizeof(float) + 4 * sizeof(float) + 3 * sizeof(float)
                   + 4 * sizeof(float);
        default:
            break;
    }
    log_gl("%s: unknown interleaved array format 0x%x\n",
           __FUNCTION__, format);
    return 0;
}

GLAPI void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride,
                                        const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(format), INT_TO_ARG(stride),
                   POINTER_TO_ARG(ptr)};
    int args_size[] = {0, 0, calc_interleaved_arrays_stride(format)};
    do_opengl_call(glInterleavedArrays_func, NULL, args, args_size);
}

#define glVertexPointer_(funcname) \
GLAPI void APIENTRY funcname(GLint size, GLenum type, GLsizei stride, \
                             const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glVertexPointer_(glVertexPointer);
glVertexPointer_(glVertexPointerEXT);

#define glNormalPointer_(funcname) \
GLAPI void APIENTRY funcname(GLenum type, GLsizei stride, const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glNormalPointer_(glNormalPointer);
glNormalPointer_(glNormalPointerEXT);

#define glIndexPointer_(funcname) \
GLAPI void APIENTRY funcname(GLenum type, GLsizei stride, const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glIndexPointer_(glIndexPointer);
glIndexPointer_(glIndexPointerEXT);

#define glColorPointer_(funcname) \
GLAPI void APIENTRY funcname(GLint size, GLenum type, GLsizei stride, \
                             const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glColorPointer_(glColorPointer);
glColorPointer_(glColorPointerEXT);

#define glSecondaryColorPointer_(funcname) \
GLAPI void APIENTRY funcname(GLint size, GLenum type, GLsizei stride, \
                             const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glSecondaryColorPointer_(glSecondaryColorPointer);
glSecondaryColorPointer_(glSecondaryColorPointerEXT);

#define glTexCoordPointer_(funcname) \
GLAPI void APIENTRY funcname(GLint size, GLenum type, GLsizei stride, \
                             const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glTexCoordPointer_(glTexCoordPointer);
glTexCoordPointer_(glTexCoordPointerEXT);

#define glEdgeFlagPointer_(funcname) \
GLAPI void APIENTRY funcname(GLsizei stride, const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(stride), POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glEdgeFlagPointer_(glEdgeFlagPointer);
glEdgeFlagPointer_(glEdgeFlagPointerEXT);

#define glFogCoordPointer_(funcname) \
GLAPI void APIENTRY funcname(GLenum type, GLsizei stride, const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(type), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glFogCoordPointer_(glFogCoordPointer);
glFogCoordPointer_(glFogCoordPointerEXT);

GLAPI void APIENTRY glWeightPointerARB(GLint size, GLenum type, GLsizei stride,
                                       const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride),
                   POINTER_TO_ARG(ptr)};
    do_opengl_call(glWeightPointerARB_func, NULL, args, NULL);
}

GLAPI void APIENTRY glMatrixIndexPointerARB(GLint size, GLenum type,
                                            GLsizei stride, const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(size), INT_TO_ARG(type), INT_TO_ARG(stride),
                   POINTER_TO_ARG(ptr)};
    do_opengl_call(glMatrixIndexPointerARB_func, NULL, args, NULL);
}

#define glMatrixIndexvARB(name, type) \
GLAPI void APIENTRY name(GLint size, const type *indices) \
{ \
    long args[] = {INT_TO_ARG(size), POINTER_TO_ARG(indices)}; \
    int args_size[] = {0, size * sizeof(type)}; \
    do_opengl_call(FUNC_NUM(name), NULL, CHECK_ARGS(args, args_size)); \
}

glMatrixIndexvARB(glMatrixIndexubvARB, GLubyte);
glMatrixIndexvARB(glMatrixIndexusvARB, GLushort);
glMatrixIndexvARB(glMatrixIndexuivARB, GLuint);

GLAPI void APIENTRY glVertexWeightfEXT(GLfloat weight)
{
    NOT_IMPLEMENTED();
}

GLAPI void APIENTRY glVertexWeightfvEXT(const GLfloat *weight)
{
    NOT_IMPLEMENTED();
}

GLAPI void APIENTRY glVertexWeightPointerEXT(GLsizei size, GLenum type,
                                             GLsizei stride,
                                             const GLvoid *pointer)
{
    NOT_IMPLEMENTED();
}

#define glVertexAttribPointer_(funcname) \
GLAPI void APIENTRY funcname(GLuint index, GLint size, GLenum type, \
                             GLboolean normalized, GLsizei stride, \
                             const GLvoid *ptr) \
{ \
    long args[] = {INT_TO_ARG(index), INT_TO_ARG(size), INT_TO_ARG(type), \
                   INT_TO_ARG(normalized), INT_TO_ARG(stride), \
                   POINTER_TO_ARG(ptr)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glVertexAttribPointer_(glVertexAttribPointer);
glVertexAttribPointer_(glVertexAttribPointerARB);

GLAPI void APIENTRY glElementPointerATI(GLenum type, const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(type), POINTER_TO_ARG(ptr)};
    do_opengl_call(glElementPointerATI_func, NULL, args, NULL);
}

#define glGetPointerv_(funcname) \
GLAPI void APIENTRY funcname(GLenum pname, void **params) \
{ \
    NOT_IMPLEMENTED(); \
    *params = NULL; \
}

glGetPointerv_(glGetPointerv);
glGetPointerv_(glGetPointervEXT);

#define glGetVertexAttribPointerv_(funcname) \
GLAPI void APIENTRY funcname(GLuint index, GLenum pname, GLvoid **pointer) \
{ \
    NOT_IMPLEMENTED(); \
    *pointer = NULL; \
}

glGetVertexAttribPointerv_(glGetVertexAttribPointerv);
glGetVertexAttribPointerv_(glGetVertexAttribPointervARB);
glGetVertexAttribPointerv_(glGetVertexAttribPointervNV);

GLAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                   const GLvoid *indices)
{
    long args[] = {INT_TO_ARG(mode), INT_TO_ARG(count), INT_TO_ARG(type),
                   POINTER_TO_ARG(indices)};
    int args_size[] = {0, 0, 0, indices ? count * datatypesize_gl(type) : 0};
    do_opengl_call(glDrawElements_func, NULL, CHECK_ARGS(args, args_size));
}

#define glDrawRangeElements_(funcname) \
GLAPI void APIENTRY funcname(GLenum mode, GLuint start, GLuint end, \
                             GLsizei count, GLenum type, \
                             const GLvoid *indices) \
{ \
    long args[] = {INT_TO_ARG(mode), INT_TO_ARG(start), INT_TO_ARG(end), \
                   INT_TO_ARG(count), INT_TO_ARG(type), \
                   POINTER_TO_ARG(indices)}; \
    int args_size[] = {0, 0, 0, 0, 0, count * datatypesize_gl(type)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glDrawRangeElements_(glDrawRangeElements);
glDrawRangeElements_(glDrawRangeElementsEXT);

#define glMultiDrawArrays_(funcname) \
GLAPI void APIENTRY funcname(GLenum mode, GLint *first, GLsizei *count, \
                             GLsizei primcount) \
{ \
    long args[] = {INT_TO_ARG(mode), POINTER_TO_ARG(first), \
                   POINTER_TO_ARG(count), INT_TO_ARG(primcount)}; \
    int args_size[] = {0, primcount * sizeof(GLint), \
                       primcount * sizeof(GLint), 0}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, args_size); \
}

glMultiDrawArrays_(glMultiDrawArrays);
glMultiDrawArrays_(glMultiDrawArraysEXT);

#define glMultiDrawElements_(funcname) \
GLAPI void APIENTRY funcname(GLenum mode, const GLsizei *count, GLenum type, \
                             const GLvoid **indices, GLsizei primcount) \
{ \
    long args[] = {INT_TO_ARG(mode), POINTER_TO_ARG(count), INT_TO_ARG(type), \
                   POINTER_TO_ARG(indices), INT_TO_ARG(primcount)}; \
    int args_size[] = {0, primcount * sizeof(int), 0, \
                       primcount * sizeof(void *), 0}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, args_size); \
}

glMultiDrawElements_(glMultiDrawElements);
glMultiDrawElements_(glMultiDrawElementsEXT);

GLAPI GLuint APIENTRY glGenSymbolsEXT(GLenum datatype, GLenum storagetype,
                                      GLenum range, GLuint components)
{
    GLuint id = 0;
    GET_CURRENT_STATE();
    long args[] = {INT_TO_ARG(datatype), INT_TO_ARG(storagetype),
                   INT_TO_ARG(range), INT_TO_ARG(components)};
    do_opengl_call(glGenSymbolsEXT_func, &id, args, NULL);
    if (id) {
        state->symbols.tab = realloc(state->symbols.tab,
                                     (state->symbols.count + 1)
                                        * sizeof(Symbol));
        state->symbols.tab[state->symbols.count].id = id;
        state->symbols.tab[state->symbols.count].datatype = datatype;
        state->symbols.tab[state->symbols.count].components = components;
        state->symbols.count++;
    }
    return id;
}

static int get_vertex_shader_var_nb_composants(GLuint id)
{
    GET_CURRENT_STATE();
    int i;
    for (i = 0; i < state->symbols.count; i++) {
        if (id >= state->symbols.tab[i].id && 
            id < state->symbols.tab[i].id + state->symbols.tab[i].components) {
            int size = 0;
            switch (state->symbols.tab[i].datatype) {
                case GL_SCALAR_EXT:
                    size = 1;
                    break;
                case GL_VECTOR_EXT:
                    size = 4;
                    break;
                case GL_MATRIX_EXT:
                    size = 16;
                    break;
                default:
                    log_gl("%s: unknown data type 0x%x\n",
                           __FUNCTION__, state->symbols.tab[i].datatype);
                    break;
            }
            return size;
        }
    }
    log_gl("%s: unknown id %d\n", id);
    return 0;
}

GLAPI void APIENTRY glSetLocalConstantEXT(GLuint id, GLenum type,
                                          const GLvoid *addr)
{
    int size = get_vertex_shader_var_nb_composants(id) * datatypesize_gl(type);
    if (size) {
        long args[] = {id, type, POINTER_TO_ARG(addr)};
        int args_size[] = {0, 0, size};
        do_opengl_call(glSetLocalConstantEXT_func, NULL,
                       CHECK_ARGS(args, args_size));
    }
}

GLAPI void APIENTRY glSetInvariantEXT(GLuint id, GLenum type,
                                      const GLvoid *addr)
{
    int size = get_vertex_shader_var_nb_composants(id) * datatypesize_gl(type);
    if (size) {
        long args[] = {id, type, POINTER_TO_ARG(addr)};
        int args_size[] = {0, 0, size};
        do_opengl_call(glSetInvariantEXT_func, NULL,
                       CHECK_ARGS(args, args_size));
    }
}

#define glVariant_(funcname, gltype) \
GLAPI void APIENTRY funcname(GLuint id, const gltype *addr) \
{ \
    int size = get_vertex_shader_var_nb_composants(id) * sizeof(gltype); \
    if (size) { \
        long args[] = {id, POINTER_TO_ARG(addr)}; \
        int args_size[] = {0, size}; \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glVariant_(glVariantbvEXT, GLbyte);
glVariant_(glVariantsvEXT, GLshort);
glVariant_(glVariantivEXT, GLint);
glVariant_(glVariantfvEXT, GLfloat);
glVariant_(glVariantdvEXT, GLdouble);
glVariant_(glVariantubvEXT, GLubyte);
glVariant_(glVariantusvEXT, GLushort);
glVariant_(glVariantuivEXT, GLuint);

GLAPI void APIENTRY glVariantPointerEXT(GLuint id, GLenum type, GLuint stride,
                                        const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(id), INT_TO_ARG(type), INT_TO_ARG(stride),
                   POINTER_TO_ARG(ptr)};
    do_opengl_call(glVariantPointerEXT_func, NULL, args, NULL);
}

#define glGetVariant_(funcname, gltype) \
GLAPI void APIENTRY funcname(GLuint id, GLenum name, gltype *addr) \
{ \
    int size = (name == GL_VARIANT_VALUE_EXT) \
        ? get_vertex_shader_var_nb_composants(id) * sizeof(gltype) \
        : sizeof(gltype); \
    if (size) { \
        long args[] = {id, name, POINTER_TO_ARG(addr)}; \
        int args_size[] = {0, 0, size}; \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glGetVariant_(glGetVariantBooleanvEXT, GLboolean);
glGetVariant_(glGetVariantIntegervEXT, GLint);
glGetVariant_(glGetVariantFloatvEXT, GLfloat);

GLAPI void APIENTRY glGetVariantPointervEXT(GLuint id, GLenum name,
                                            GLvoid **addr)
{
    NOT_IMPLEMENTED();
    *addr = NULL;
}

#define glGetInvariant_(funcname, gltype) \
GLAPI void APIENTRY funcname(GLuint id, GLenum name, gltype* addr) \
{ \
    int size = (name == GL_INVARIANT_VALUE_EXT) \
        ? get_vertex_shader_var_nb_composants(id) * sizeof(gltype) \
        : sizeof(gltype); \
    if (size) { \
        long args[] = {id, name, POINTER_TO_ARG(addr)}; \
        int args_size[] = {0, 0, size}; \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glGetInvariant_(glGetInvariantBooleanvEXT, GLboolean);
glGetInvariant_(glGetInvariantIntegervEXT, GLint);
glGetInvariant_(glGetInvariantFloatvEXT, GLfloat);

#define glGetLocalConstant_(funcname, gltype) \
GLAPI void APIENTRY funcname(GLuint id, GLenum name, gltype* addr) \
{ \
    int size = (name == GL_LOCAL_CONSTANT_VALUE_EXT) \
        ? get_vertex_shader_var_nb_composants(id) * sizeof(gltype) \
        : sizeof(gltype); \
    if (size) { \
        long args[] = {id, name, POINTER_TO_ARG(addr)}; \
        int args_size[] = {0, 0, size}; \
        do_opengl_call(FUNC_NUM(funcname), NULL, \
                       CHECK_ARGS(args, args_size)); \
    } \
}

glGetLocalConstant_(glGetLocalConstantBooleanvEXT, GLboolean);
glGetLocalConstant_(glGetLocalConstantIntegervEXT, GLint);
glGetLocalConstant_(glGetLocalConstantFloatvEXT, GLfloat);

static void glShaderSource_(int func_number, GLhandleARB handle, GLsizei size,
                            const GLcharARB **tab_prog,
                            const GLint *tab_length)
{
    if (size <= 0 || tab_prog == NULL) {
        log_gl("%s: size <= 0 || tab_prog == NULL\n", __FUNCTION__);
        return;
    }
    int total_length = 0;
    int *my_tab_length = malloc(sizeof(int) * size);
    int i;
    for (i = 0; i < size; i++) {
        if (tab_prog[i] == NULL) {
            log_gl("%s: tab_prog[%d] == NULL\n", __FUNCTION__, i);
            free(my_tab_length);
            return;
        }
        my_tab_length[i] = (tab_length && tab_length[i])
                           ? tab_length[i] : strlen(tab_prog[i]);
        total_length += my_tab_length[i];
    }
    GLcharARB *all_progs = malloc(total_length+1);
    int acc_length = 0;
    for(i = 0; i < size; i++) {
        char *str_tmp = all_progs + acc_length;
        memcpy(str_tmp, tab_prog[i], my_tab_length[i]);
        str_tmp[my_tab_length[i]] = 0;
        DEBUGLOG_GL("glShaderSource[%d] : %s\n", i, str_tmp);
        char *version_ptr = strstr(str_tmp, "#version");
        if (version_ptr && version_ptr != str_tmp) {
            /* ATI driver won't be happy if "#version" is not at beginning 
             * of program -- Necessary for "Danger from the Deep 0.3.0" */
            int offset = version_ptr - str_tmp;
            char *eol = strchr(version_ptr, '\n');
            if (eol) {
                int len = eol - version_ptr + 1;
                memcpy(str_tmp, tab_prog[i] + offset, len);
                memcpy(str_tmp + len, tab_prog[i], offset);
            }
        }
        acc_length += my_tab_length[i];
    }
    long args[] = {INT_TO_ARG(handle), INT_TO_ARG(size),
                   POINTER_TO_ARG(all_progs), POINTER_TO_ARG(my_tab_length)};
    int args_size[] = {0, 0, total_length, sizeof(int) * size};
    do_opengl_call(func_number, NULL, CHECK_ARGS(args, args_size));
    free(my_tab_length);
    free(all_progs);
}

GLAPI void APIENTRY glShaderSourceARB(GLhandleARB handle, GLsizei size,
                                      const GLcharARB **tab_prog,
                                      const GLint *tab_length)
{
    glShaderSource_(glShaderSourceARB_func, handle, size, tab_prog,
                    tab_length);
}

GLAPI void APIENTRY glShaderSource(GLhandleARB handle, GLsizei size,
                                   const GLcharARB **tab_prog,
                                   const GLint *tab_length)
{
    glShaderSource_(glShaderSource_func, handle, size, tab_prog, tab_length);
}

GLAPI void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei maxLength,
                                        GLsizei *length, GLchar *infoLog)
{
    int fake_length;
    if (length == NULL) {
        length = &fake_length;
    }
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(maxLength),
                   POINTER_TO_ARG(length), POINTER_TO_ARG(infoLog)};
    int args_size[] = {0, 0, sizeof(int), maxLength};
    do_opengl_call(glGetProgramInfoLog_func, NULL,
                   CHECK_ARGS(args, args_size));
    DEBUGLOG_GL("%s: %s\n", __FUNCTION__, infoLog);
}

GLAPI void APIENTRY glGetProgramStringARB(GLenum target, GLenum pname,
                                          GLvoid *string)
{
    int size = 0;
    glGetProgramivARB(target, GL_PROGRAM_LENGTH_ARB, &size);
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(pname),
                   POINTER_TO_ARG(string)};
    int args_size[] = {0, 0, size};
    do_opengl_call(glGetProgramStringARB_func, NULL,
                   CHECK_ARGS(args, args_size));
    DEBUGLOG_GL("%s, pname=0x%x: %s\n", __FUNCTION__, pname, string);
}

GLAPI void APIENTRY glGetProgramStringNV(GLenum target, GLenum pname,
                                         GLvoid *string)
{
    int size = 0;
    glGetProgramivNV(target, GL_PROGRAM_LENGTH_NV, &size);
    long args[] = {INT_TO_ARG(target), INT_TO_ARG(pname),
                   POINTER_TO_ARG(string)};
    int args_size[] = {0, 0, size};
    do_opengl_call(glGetProgramStringNV_func, NULL,
                   CHECK_ARGS(args, args_size));
    DEBUGLOG_GL("%s, pname=0x%x: %s\n", __FUNCTION__, pname, string);
}

GLAPI void APIENTRY glGetInfoLogARB(GLhandleARB object, GLsizei maxLength,
                                    GLsizei *length, GLcharARB *infoLog)
{
    int fake_length;
    if (length == NULL) {
        length = &fake_length;
    }
    long args[] = {INT_TO_ARG(object), INT_TO_ARG(maxLength),
                   POINTER_TO_ARG(length), POINTER_TO_ARG(infoLog)};
    int args_size[] = {0, 0, sizeof(int), maxLength};
    do_opengl_call(glGetInfoLogARB_func, NULL, CHECK_ARGS(args, args_size));
    DEBUGLOG_GL("%s: %s\n", infoLog);
}

GLAPI void APIENTRY glGetAttachedObjectsARB(GLhandleARB program,
                                            GLsizei maxCount, GLsizei *count,
                                            GLhandleARB *objects)
{
    int fake_count;
    if (count == NULL) {
        count = &fake_count;
    }
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(maxCount),
                   POINTER_TO_ARG(count), POINTER_TO_ARG(objects)};
    int args_size[] = {0, 0, sizeof(int), maxCount * sizeof(int)};
    do_opengl_call(glGetAttachedObjectsARB_func, NULL,
                   CHECK_ARGS(args, args_size));
}

GLAPI void APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount,
                                         GLsizei *count, GLuint *shaders)
{
    int fake_count;
    if (count == NULL) {
        count = &fake_count;
    }
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(maxCount),
                   POINTER_TO_ARG(count), POINTER_TO_ARG(shaders)};
    int args_size[] = {0, 0, sizeof(int), maxCount * sizeof(int)};
    do_opengl_call(glGetAttachedShaders_func, NULL,
                   CHECK_ARGS(args, args_size));
}

#define glGetActiveUniform_(funcname) \
GLAPI void APIENTRY funcname(GLuint program, GLuint index, GLsizei maxLength, \
                             GLsizei *length, GLint *size, GLenum *type, \
                             GLcharARB *name) \
{ \
    int fake_length; \
    if (length == NULL) { \
        length = &fake_length; \
    } \
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(index), \
                   INT_TO_ARG(maxLength), POINTER_TO_ARG(length), \
                   POINTER_TO_ARG(size), POINTER_TO_ARG(type), \
                   POINTER_TO_ARG(name)}; \
    int args_size[] = {0, 0, 0, sizeof(int), sizeof(int), sizeof(int), \
                       maxLength}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGetActiveUniform_(glGetActiveUniform);
glGetActiveUniform_(glGetActiveUniformARB);

GLAPI void APIENTRY glGetActiveVaryingNV(GLuint program, GLuint index,
                                         GLsizei bufSize, GLsizei *length,
                                         GLsizei *size, GLenum *type,
                                         GLchar *name)
{
    int fake_length;
    if (length == NULL) {
        length = &fake_length;
    }
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(index),
                   INT_TO_ARG(bufSize), POINTER_TO_ARG(length),
                   POINTER_TO_ARG(size), POINTER_TO_ARG(type),
                   POINTER_TO_ARG(name)};
    int args_size[] = {0, 0, 0, sizeof(int), sizeof(int), sizeof(int),
                       bufSize};
    do_opengl_call(glGetActiveVaryingNV_func, NULL,
                   CHECK_ARGS(args, args_size));
}

#define glGetUniform_(funcname) \
GLAPI void APIENTRY funcname(GLuint program, GLint location, void *params) \
{ \
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(location), \
                   POINTER_TO_ARG(params)}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, args, NULL); \
}

glGetUniform_(glGetUniformfv);
glGetUniform_(glGetUniformfvARB);
glGetUniform_(glGetUniformiv);
glGetUniform_(glGetUniformivARB);
glGetUniform_(glGetUniformuivEXT);

#define glGetShaderSource_(funcname) \
GLAPI void APIENTRY funcname(GLuint shader, GLsizei maxLength, \
                             GLsizei *length, GLchar *source) \
{ \
    int fake_length; \
    if (length == NULL) { \
        length = &fake_length; \
    } \
    long args[] = {INT_TO_ARG(shader), INT_TO_ARG(maxLength), \
                   POINTER_TO_ARG(length), POINTER_TO_ARG(source)}; \
    int args_size[] = {0, 0, sizeof(int), maxLength}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
    DEBUGLOG_GL("%s: %s\n", __FUNCTION__, source); \
}

glGetShaderSource_(glGetShaderSource);
glGetShaderSource_(glGetShaderSourceARB);
glGetShaderSource_(glGetShaderInfoLog);

GLAPI GLuint APIENTRY glNewObjectBufferATI(GLsizei size, const GLvoid *pointer,
                                           GLenum usage)
{
    int buffer = 0;
    long args[] = {INT_TO_ARG(size), POINTER_TO_ARG(pointer),
                   INT_TO_ARG(usage)};
    int args_size[] = {0, pointer ? size : 0, 0};
    do_opengl_call(glNewObjectBufferATI_func, &buffer,
                   CHECK_ARGS(args, args_size));
    return buffer;
}

GLAPI void APIENTRY glUpdateObjectBufferATI(GLuint buffer, GLuint offset,
                                            GLsizei size,
                                            const GLvoid *pointer,
                                            GLenum preserve)
{
    long args[] = {INT_TO_ARG(buffer), INT_TO_ARG(offset), INT_TO_ARG(size),
                   POINTER_TO_ARG(pointer), INT_TO_ARG(preserve)};
    int args_size[] = {0, 0, 0, size, 0};
    do_opengl_call(glUpdateObjectBufferATI_func, NULL,
                   CHECK_ARGS(args, args_size));
}

GLAPI GLvoid *APIENTRY glMapObjectBufferATI(GLuint buffer)
{
    NOT_IMPLEMENTED();
    return NULL;
}

GLAPI void APIENTRY glUnmapObjectBufferATI(GLuint buffer)
{
    NOT_IMPLEMENTED();
}

#define glGetActiveAttrib_(funcname) \
GLAPI void APIENTRY funcname(GLuint program, GLuint index, \
                             GLsizei maxLength, GLsizei *length, GLint *size, \
                             GLenum *type, GLchar *name) \
{ \
    int fake_length; \
    if (length == NULL) { \
        length = &fake_length; \
    } \
    long args[] = {INT_TO_ARG(program), INT_TO_ARG(index), \
                   INT_TO_ARG(maxLength), POINTER_TO_ARG(length), \
                   POINTER_TO_ARG(size), POINTER_TO_ARG(type), \
                   POINTER_TO_ARG(name)}; \
    int args_size[] = {0, 0, 0, sizeof(int), sizeof(int), sizeof(int), \
                       maxLength}; \
    do_opengl_call(FUNC_NUM(funcname), NULL, CHECK_ARGS(args, args_size)); \
}

glGetActiveAttrib_(glGetActiveAttrib);
glGetActiveAttrib_(glGetActiveAttribARB);

GLAPI void APIENTRY glGetDetailTexFuncSGIS(GLenum target, GLfloat *points)
{
    int npoints = 0;
    glGetTexParameteriv(target, GL_DETAIL_TEXTURE_FUNC_POINTS_SGIS, &npoints);
    long args[] = {INT_TO_ARG(target), POINTER_TO_ARG(points)};
    int args_size[] = {0, 2 * npoints * sizeof(GLfloat)};
    do_opengl_call(glGetDetailTexFuncSGIS_func, NULL,
                   CHECK_ARGS(args, args_size));
}

GLAPI void APIENTRY glGetSharpenTexFuncSGIS(GLenum target, GLfloat *points)
{
    int npoints = 0;
    glGetTexParameteriv(target, GL_SHARPEN_TEXTURE_FUNC_POINTS_SGIS, &npoints);
    long args[] = {INT_TO_ARG(target), POINTER_TO_ARG(points)};
    int args_size[] = {0, 2 * npoints * sizeof(GLfloat)};
    do_opengl_call(glGetSharpenTexFuncSGIS_func, NULL,
                   CHECK_ARGS(args, args_size));
}

#define glColorTable_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum internalformat, \
                             GLsizei width, GLenum format, GLenum type, \
                             const GLvoid *table) \
{ \
    NOT_IMPLEMENTED(); \
}

glColorTable_(glColorTable);
glColorTable_(glColorTableEXT);

#define glColorSubTable_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLsizei start, GLsizei count, \
                             GLenum format, GLenum type, const GLvoid *data) \
{ \
    NOT_IMPLEMENTED(); \
}

glColorSubTable_(glColorSubTable);
glColorSubTable_(glColorSubTableEXT);

#define glGetColorTable_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum format, GLenum type, \
                             GLvoid *table) \
{ \
    NOT_IMPLEMENTED(); \
}

glGetColorTable_(glGetColorTable);
glGetColorTable_(glGetColorTableEXT);

#define glConvolutionFilter1D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum internalformat, \
                             GLsizei width, GLenum format, GLenum type, \
                             const GLvoid *image) \
{ \
    NOT_IMPLEMENTED(); \
}

glConvolutionFilter1D_(glConvolutionFilter1D);
glConvolutionFilter1D_(glConvolutionFilter1DEXT);

#define glConvolutionFilter2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum internalformat, \
                             GLsizei width, GLsizei height, GLenum format, \
                             GLenum type, const GLvoid *image) \
{ \
    NOT_IMPLEMENTED(); \
}

glConvolutionFilter2D_(glConvolutionFilter2D);
glConvolutionFilter2D_(glConvolutionFilter2DEXT);

#define glGetConvolutionFilter_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum format, GLenum type, \
                             GLvoid *image) \
{ \
    NOT_IMPLEMENTED(); \
}

glGetConvolutionFilter_(glGetConvolutionFilter);
glGetConvolutionFilter_(glGetConvolutionFilterEXT);

#define glGetSeparableFilter_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum format, GLenum type, \
                             GLvoid *row, GLvoid *column, GLvoid *span) \
{ \
    NOT_IMPLEMENTED(); \
}

glGetSeparableFilter_(glGetSeparableFilter);
glGetSeparableFilter_(glGetSeparableFilterEXT);

#define glSeparableFilter2D_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLenum internalformat, \
                             GLsizei width, GLsizei height, GLenum format, \
                             GLenum type, const GLvoid *row, \
                             const GLvoid *column) \
{ \
    NOT_IMPLEMENTED(); \
}

glSeparableFilter2D_(glSeparableFilter2D);
glSeparableFilter2D_(glSeparableFilter2DEXT);

#define glGetHistogram_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLboolean reset, GLenum format, \
                             GLenum type, GLvoid *values) \
{ \
    NOT_IMPLEMENTED(); \
}

glGetHistogram_(glGetHistogram);
glGetHistogram_(glGetHistogramEXT);

#define glGetMinmax_(funcname) \
GLAPI void APIENTRY funcname(GLenum target, GLboolean reset, GLenum format, \
                             GLenum type, GLvoid *values) \
{ \
    NOT_IMPLEMENTED(); \
}

glGetMinmax_(glGetMinmax);
glGetMinmax_(glGetMinmaxEXT);

GLAPI void APIENTRY glPixelDataRangeNV(GLenum target, GLsizei length,
                                       GLvoid *pointer)
{
    /* do nothing is a possible implementation... */
}

GLAPI void APIENTRY glFlushPixelDataRangeNV(GLenum target)
{
    /* do nothing is a possible implementation... */
}

GLAPI void APIENTRY glVertexArrayRangeNV(GLsizei size, const GLvoid *ptr)
{
    /* do nothing is a possible implementation... */
}

GLAPI void APIENTRY glFlushVertexArrayRangeNV(void)
{
    /* do nothing is a possible implementation... */
}

GLAPI void APIENTRY glTexFilterFuncSGIS(GLenum target, GLenum filter,
                                        GLsizei n, const GLfloat *weights)
{
    NOT_IMPLEMENTED();
}

GLAPI void APIENTRY glGetTexFilterFuncSGIS(GLenum target, GLenum filter,
                                           GLfloat *weights)
{
    NOT_IMPLEMENTED();
}

GLAPI void APIENTRY glTangentPointerEXT(GLenum type, GLsizei stride,
                                        const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(type), INT_TO_ARG(stride), POINTER_TO_ARG(ptr)};
    do_opengl_call(glTangentPointerEXT_func, NULL, args, NULL);
}

GLAPI void APIENTRY glBinormalPointerEXT(GLenum type, GLsizei stride,
                                         const GLvoid *ptr)
{
    long args[] = {INT_TO_ARG(type), INT_TO_ARG(stride), POINTER_TO_ARG(ptr)};
    do_opengl_call(glBinormalPointerEXT_func, NULL, args, NULL);
}

GLAPI void *APIENTRY glXAllocateMemoryNV(GLsizei size, GLfloat readfreq,
                                         GLfloat writefreq, GLfloat priority)
{
    return malloc(size);
}

GLAPI void APIENTRY glXFreeMemoryNV(GLvoid *pointer)
{
    free(pointer);
}

static unsigned int str_hash(const void *v)
{
    /* 31 bit hash function */
    const signed char *p = v;
    unsigned int h = *p;

    if (h) {
        for (p += 1; *p != '\0'; p++) {
            h = (h << 5) - h + *p;
        }
    }

    return h;
}

const char *glXQueryExtensionsString(Display *dpy, int screen)
{
    static const char *glx_extensions =
        "GLX_EXT_visual_info "
        "GLX_EXT_visual_rating "
        "GLX_SGI_swap_control "
        "GLX_ARB_multisample "
        "GLX_ARB_get_proc_address ";
    return glx_extensions;
}

typedef struct {
    XVisualInfo *vis;
    int visualid;
    GLXFBConfig fbconfig;
} AssocVisualInfoVisualId;

#define MAX_SIZE_TAB_ASSOC_VISUALINFO_VISUALID 100
AssocVisualInfoVisualId tabAssocVisualInfoVisualId[MAX_SIZE_TAB_ASSOC_VISUALINFO_VISUALID];
int nEltTabAssocVisualInfoVisualId = 0;

static const char *get_attrname_fromvalue(int val)
{
    int i;
    static char buffer[80];
    for (i = 0; i < N_REQUESTED_ATTRIBS; i++) {
        if (tabRequestedAttribsPair[i].val == val) {
            return tabRequestedAttribsPair[i].name;
        }
    }
    sprintf(buffer, "(unknown name = %d, 0x%X)", val, val);
    return buffer;
}

static int compute_len_of_attrlist_inczero(const int *attribList,
                                           int booleanMustHaveValue)
{
    // (re)assign debug_gl value here to ensure all glx calls are logged
    debug_gl = getenv("DEBUG_GL") != NULL;
    DEBUGLOG_GL("attribList = \n");
    int i = 0;
    while (attribList[i]) {
        if (booleanMustHaveValue ||
            !(attribList[i] == GLX_USE_GL ||
              attribList[i] == GLX_RGBA ||
              attribList[i] == GLX_DOUBLEBUFFER ||
              attribList[i] == GLX_STEREO)) {
            DEBUGLOG_GL("%s = %d\n",
                        get_attrname_fromvalue(attribList[i]),
                        attribList[i + 1]);
            i += 2;
        } else {
            DEBUGLOG_GL("%s\n", get_attrname_fromvalue(attribList[i]));
            i++;
        }
    }
    DEBUGLOG_GL("\n");
    return i + 1;
}

XVisualInfo *glXChooseVisual(Display *dpy, int screen, int *attribList)
{
    XVisualInfo temp;
    temp.screen = screen;
    temp.depth = DefaultDepth(dpy, screen);
    temp.class = DefaultVisual(dpy, screen)->class;
    temp.visualid = DefaultVisual(dpy, screen)->visualid;
    long mask = VisualScreenMask | VisualDepthMask |
                VisualClassMask  | VisualIDMask;
    int n;
    XVisualInfo *vis = XGetVisualInfo(dpy, mask, &temp, &n);
    if (vis == NULL) {
        log_gl("cannot get visual from client side\n");
    }
    
    int visualid = 0;
    long args[] = {INT_TO_ARG(vis->depth), POINTER_TO_ARG(attribList)};
    int args_size[] = {0,
        sizeof(int) * compute_len_of_attrlist_inczero(attribList, 0)};
    do_opengl_call(glXChooseVisual_func, &visualid,
                   CHECK_ARGS(args, args_size));
    
    if (visualid) {
        assert(nEltTabAssocVisualInfoVisualId
               < MAX_SIZE_TAB_ASSOC_VISUALINFO_VISUALID);
        int i;
        for(i = 0; i < nEltTabAssocVisualInfoVisualId; i++) {
            if (tabAssocVisualInfoVisualId[i].vis == vis) {
                break;
            }
        }
        if (i == nEltTabAssocVisualInfoVisualId) {
            nEltTabAssocVisualInfoVisualId++;
        }
        tabAssocVisualInfoVisualId[i].vis = vis;
        tabAssocVisualInfoVisualId[i].fbconfig = 0;
        tabAssocVisualInfoVisualId[i].visualid = visualid;
    } else {
        if (vis) {
            XFree(vis);
            vis = NULL;
        }
    }
    
    DEBUGLOG_GL("glXChooseVisual returning vis %p (visualid=%d, 0x%X)\n",
                vis, visualid, visualid);
    
    return vis;
}

const char *glXQueryServerString(Display *dpy, int screen, int name)
{
    switch (name) {
        case 1: // GLX_VENDOR
            return "QEMU";
        case 2: // GLX_VERSION
            return "1.4";
        case 3: // GLX_EXTENSIONS
            return glXQueryExtensionsString(dpy, screen);
        default:
            break;
    }
    return NULL;
}

const char *glXGetClientString(Display *dpy, int name)
{
    return glXQueryServerString(dpy, 0, name);
}

static void create_context(GLXContext context, GLXContext shareList)
{
    glstates = realloc(glstates, (nbGLStates + 1) * sizeof(GLState *));
    glstates[nbGLStates] = new_gl_state();
    glstates[nbGLStates]->ref = 1;
    glstates[nbGLStates]->context = context;
    glstates[nbGLStates]->shareList = shareList;
    glstates[nbGLStates]->pbuffer = 0;
    glstates[nbGLStates]->viewport.width = 0;
    glstates[nbGLStates]->shm_info = 0;
    glstates[nbGLStates]->ximage = 0;
    if (shareList) {
        int i;
        for (i = 0; i < nbGLStates; i++) {
            if (glstates[i]->context == shareList) {
                glstates[i]->ref++;
                break;
            }
        }
        if (i == nbGLStates) {
            log_gl("unknown shareList %p\n", (void *)shareList);
        }
    }
    nbGLStates++;
}

GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis,
                            GLXContext shareList, Bool direct)
{
    LOCK(glXCreateContext_func);
    int visualid = 0;
    int isFbConfVisual = 0;
    int i;
    
    for (i = 0; i < nEltTabAssocVisualInfoVisualId; i++) {
        if (tabAssocVisualInfoVisualId[i].vis == vis) {
            if (tabAssocVisualInfoVisualId[i].fbconfig != NULL) {
                isFbConfVisual = 1;
            }
            visualid = tabAssocVisualInfoVisualId[i].visualid;
            DEBUGLOG_GL("found visualid %d corresponding to vis %p\n",
                        visualid, vis);
            break;
        }
    }
    
    if (i == nEltTabAssocVisualInfoVisualId) {
        visualid = vis->visualid;
        DEBUGLOG_GL("not found vis %p in table, visualid=%d\n", vis, visualid);
    }
    GLXContext ctxt = NULL;
    long args[] = {INT_TO_ARG(visualid), INT_TO_ARG(shareList)};
    do_opengl_call_no_lock(glXCreateContext_func, &ctxt, args, NULL);
    
    if (ctxt) {
        create_context(ctxt, shareList);
        glstates[nbGLStates-1]->isAssociatedToFBConfigVisual = isFbConfVisual;
    }
    UNLOCK(glXCreateContext_func);
    return ctxt;
}

GLXContext glXGetCurrentContext(void)
{
    GET_CURRENT_STATE();
    DEBUGLOG_GL("glXGetCurrentContext() -> %p\n", state->context);
    return state->context;
}

GLXDrawable glXGetCurrentDrawable(void)
{
    GET_CURRENT_STATE();
    DEBUGLOG_GL("glXGetCurrentDrawable() -> %p\n",
                (void *)state->current_drawable);
    return state->current_drawable;
}

static void free_state_shm(Display *dpy, GLState *state)
{
    if (state->shm_info) {
        if (state->shm_info->shmaddr && state->shm_info->shmid >= 0) {
            XShmDetach(dpy, state->shm_info);
        }
        if (state->ximage) {
            XDestroyImage(state->ximage);
            state->ximage = 0;
            XFreeGC(dpy, state->xgc);
        }
        if (state->shm_info->shmid >= 0) {
            if (state->shm_info->shmaddr) {
                shmdt(state->shm_info->shmaddr);
                state->shm_info->shmaddr = 0;
            }
            shmctl(state->shm_info->shmid, IPC_RMID, NULL);
        }
        free(state->shm_info);
        state->shm_info = 0;
        XSync(dpy, True);
    }
}

static void free_context(Display *dpy, int i, GLState *state)
{
    if (state->pbuffer) {
        glXDestroyPbuffer(dpy, state->pbuffer);
    }
    free_state_shm(dpy, state);
    free(state);
    memmove(&state, &glstates[i + 1],
            (nbGLStates - i - 1) * sizeof(GLState *));
    nbGLStates--;
}

GLAPI void APIENTRY glXDestroyContext(Display *dpy, GLXContext ctx)
{
    LOCK(glXDestroyContext_func);
    GET_CURRENT_STATE();
    int i;
    for (i = 0; i < nbGLStates; i++) {
        if (glstates[i]->context == ctx) {
            long args[] = {POINTER_TO_ARG(ctx)};
            do_opengl_call_no_lock(glXDestroyContext_func, NULL, args, NULL);
            if (ctx == state->context) {
                SET_CURRENT_STATE(NULL);
            }
            
            GLXContext shareList = glstates[i]->shareList;
            
            glstates[i]->ref--;
            if (glstates[i]->ref == 0) {
                free_context(dpy, i, glstates[i]);
            }
            
            if (shareList) {
                for (i = 0; i < nbGLStates; i++) {
                    if (glstates[i]->context == shareList) {
                        glstates[i]->ref--;
                        if (glstates[i]->ref == 0) {
                            free_context(dpy, i, glstates[i]);
                        }
                        break;
                    }
                }
            }
            break;
        }
    }
    UNLOCK(glXDestroyContext_func);
}

Bool glXQueryVersion(Display *dpy, int *maj, int *min)
{
    if (maj) {
        *maj = 1;
    }
    if (min) {
        *min = 4;
    }
    return True;
}

static void get_window_pos(Display *dpy, Window win, WindowPosStruct *pos)
{
    int x, y;
    Window root = DefaultRootWindow(dpy);
    XWindowAttributes window_attributes_return;
    XGetWindowAttributes(dpy, win, &window_attributes_return);
    Window child;
    XTranslateCoordinates(dpy, win, root, 0, 0, &x, &y, &child);
    pos->x = x;
    pos->y = y;
    pos->width = window_attributes_return.width;
    pos->height = window_attributes_return.height;
    pos->map_state = window_attributes_return.map_state;
}

static Bool create_drawable(GLState *state, Display *dpy, GLXDrawable drawable)
{
    Bool ret = False;
    DEBUGLOG_GL("drawable 0x%X: %dx%d (depth %d)\n", (int)drawable,
                state->drawable_width, state->drawable_height,
                state->drawable_depth);
    free_state_shm(dpy, state);
    state->shm_info = (XShmSegmentInfo *)malloc(sizeof(XShmSegmentInfo));
    if (state->shm_info) {
        memset(state->shm_info, 0, sizeof(XShmSegmentInfo));
        state->shm_info->shmid = -1;
        state->ximage = XShmCreateImage(dpy,
                                        &state->drawable_visual,
                                        state->drawable_depth,
                                        ZPixmap, NULL, state->shm_info,
                                        state->drawable_width,
                                        state->drawable_height);
        if (state->ximage) {
            state->xgc = XCreateGC(dpy, drawable, 0, NULL);
            state->shm_info->shmid = shmget(IPC_PRIVATE,
                                            state->ximage->bytes_per_line *
                                            state->ximage->height,
                                            IPC_CREAT | 0777);
            if (state->shm_info->shmid >= 0) {
                state->shm_info->shmaddr = shmat(state->shm_info->shmid,
                                                 NULL, 0);
                if (state->shm_info->shmaddr) {
                    state->shm_info->readOnly = False;
                    state->ximage->data = state->shm_info->shmaddr;
                    if (XShmAttach(dpy, state->shm_info)) {
                        ret = True;
#ifdef QEMUGL_MODULE
                        unsigned int devargs[4] = {
                            state->drawable_width,
                            state->drawable_height,
                            state->ximage->bits_per_pixel / 8,
                            state->ximage->bytes_per_line
                        };
                        ioctl(glfd, QEMUGL_FIOSTBUF, &devargs);
#endif // QEMUGL_MODULE
                    }
                } else {
                    log_gl("shmat failed with result %d\n",
                           errno);
                }
            } else {
                log_gl("shmget failed with result %d\n",
                       state->shm_info->shmid);
            }
        }
    }
    return ret;
}

#ifndef QEMUGL_MODULE
#define WINCPY(name, type) \
static void wincpy_##name(void *p, volatile unsigned int *hw, \
                          int width, int height, int pitch) \
{ \
    int extra = pitch - width; \
    type *pp = (type *)p; \
    while (height--) { \
        int x = width >> 2; \
        while (x--) { \
            *(pp++) = (type)hw[7]; \
            *(pp++) = (type)hw[7]; \
            *(pp++) = (type)hw[7]; \
            *(pp++) = (type)hw[7]; \
        } \
        for (x = width & 3; x--; ) { \
            *(pp++) = (type)hw[7]; \
        } \
        pp += extra; \
    } \
}

WINCPY(byte, uint8_t);
WINCPY(short, uint16_t);
WINCPY(word, uint32_t);
#endif // QEMUGL_MODULE

static void update_win(Display *dpy, Window win)
{
    GET_CURRENT_STATE();
    if (state->ximage) {
#ifdef QEMUGL_MODULE
        ioctl(glfd, QEMUGL_FIOCPBUF, state->ximage->data);
#else
#ifdef __arm__
        volatile unsigned int *hw = virt_addr;
#else
#error unsupported architecture!
#endif
        int bpp = (state->drawable_depth + 7) / 8;
        int pitch = state->ximage->bytes_per_line;
        switch (bpp) {
            case 1:
                wincpy_byte(state->ximage->data, hw, state->drawable_width,
                            state->drawable_height, pitch);
                break;
            case 2:
                wincpy_short(state->ximage->data, hw, state->drawable_width,
                             state->drawable_height, pitch >> 1);
                break;
            case 4:
                wincpy_word(state->ximage->data, hw, state->drawable_width,
                            state->drawable_height, pitch >> 2);
                break;
            default:
                log_gl("unsupported buffer depth %d bytes/pixel\n", bpp);
                break;
        }
#endif // QEMUGL_MODULE
        XShmPutImage(dpy, win, state->xgc, state->ximage, 0, 0, 0, 0,
                     state->drawable_width, state->drawable_height, False);
        XFlush(dpy);
    }
    WindowPosStruct pos;
    get_window_pos(dpy, win, &pos);
    if (pos.width != state->drawable_width ||
        pos.height != state->drawable_height) {
        state->drawable_width = pos.width;
        state->drawable_height = pos.height;
        create_drawable(state, dpy, win);
        long args[] = {INT_TO_ARG(win), INT_TO_ARG(pos.width),
                       INT_TO_ARG(pos.height)};
        do_opengl_call_no_lock(_resizeDrawable_func, NULL, args, NULL);
    }
}

static Bool glXMakeCurrent_no_lock(Display *dpy, GLXDrawable drawable,
                                   GLXContext ctx)
{
    Bool ret = True;
    int i;
    GET_CURRENT_STATE();
    if (ctx) {
        for (i = 0; i < nbGLStates; i++) {
            if (glstates[i]->context == ctx) {
                break;
            }
        }
        if (i >= nbGLStates) {
            log_gl("unknown context\n");
            return False;
        }
        state = glstates[i];
        SET_CURRENT_STATE(state);
        if (drawable) {
            XWindowAttributes window_attributes_return;
            XGetWindowAttributes(dpy, drawable, &window_attributes_return);
            state->viewport.width = window_attributes_return.width;
            state->viewport.height = window_attributes_return.height;
            state->drawable_width = window_attributes_return.width;
            state->drawable_height = window_attributes_return.height;
            if (drawable != state->current_drawable) {
                memcpy(&state->drawable_visual, window_attributes_return.visual,
                       sizeof(state->drawable_visual));
                state->drawable_depth = state->drawable_visual.bits_per_rgb;
                XVisualInfo temp, *xvis;
                temp.visualid = XVisualIDFromVisual(&state->drawable_visual);
                int nvis;
                xvis = XGetVisualInfo(dpy, VisualIDMask, &temp, &nvis);
                if (xvis) {
                    state->drawable_depth = xvis->depth;
                    XFree(xvis);
                }
                state->current_drawable = drawable;
                ret = create_drawable(state, dpy, drawable);
                if (ret) {
                    long args[] = {INT_TO_ARG(drawable),
                                   INT_TO_ARG(state->shm_info->shmaddr),
                                   INT_TO_ARG(state->ximage->bits_per_pixel),
                                   INT_TO_ARG(state->drawable_width),
                                   INT_TO_ARG(state->drawable_height)};
                    do_opengl_call_no_lock(_createDrawable_func, NULL, args, NULL);
                }
            }
        }
    }
    if (!drawable || !ret) {
        free_state_shm(dpy, state);
    }
    
    state->display = dpy;
    state->context = ctx;
    state->current_drawable = drawable;
    state->current_read_drawable = drawable;

    if (ret) {
        long args[] = {INT_TO_ARG(drawable),INT_TO_ARG(ctx)};
        do_opengl_call_no_lock(glXMakeCurrent_func, NULL /*&ret*/, args, NULL);
    }
    return ret;
}

GLAPI Bool APIENTRY glXMakeCurrent(Display *dpy, GLXDrawable drawable,
                                   GLXContext ctx)
{
    Bool ret;
    LOCK(glXMakeCurrent_func);
    ret = glXMakeCurrent_no_lock(dpy, drawable, ctx);
    UNLOCK(glXMakeCurrent_func);
    return ret;
}

GLAPI void APIENTRY glXCopyContext(Display *dpy, GLXContext src,
                                   GLXContext dst, unsigned long mask)
{
    long args[] = {INT_TO_ARG(src), INT_TO_ARG(dst), INT_TO_ARG(mask)};
    do_opengl_call(glXCopyContext_func, NULL, args, NULL);
}

GLAPI Bool APIENTRY glXIsDirect(Display *dpy, GLXContext ctx)
{
    return True;
}

GLAPI int APIENTRY glXGetConfig(Display *dpy, XVisualInfo *vis,
                                int attrib, int *value)
{
    if (vis == NULL || value == NULL) {
        return 0;
    }
    LOCK(glXGetConfig_func);
    
    int ret = 0;
    int visualid = 0;
    int i, j;
    for (i = 0; i < nEltTabAssocVisualInfoVisualId; i++) {
        if (vis == tabAssocVisualInfoVisualId[i].vis) {
            visualid = tabAssocVisualInfoVisualId[i].visualid;
            DEBUGLOG_GL("found visualid %d corresponding to vis %p\n",
                        visualid, vis);
            break;
        }
    }
    if (i == nEltTabAssocVisualInfoVisualId) {
        DEBUGLOG_GL("not found vis %p in table\n", vis);
        visualid = vis->visualid;
    }
    
    /* Optimization */
    for (i = 0; i < nbConfigs; i++) {
        if (visualid == configs[i].visualid) {
            for (j = 0; j < configs[i].nbAttribs; j++) {
                if (configs[i].attribs[j].attrib == attrib) {
                    *value = configs[i].attribs[j].value;
                    ret = configs[i].attribs[j].ret;
                    DEBUGLOG_GL("glXGetConfig(%s)=%d (%d)\n",
                                get_attrname_fromvalue(attrib), *value, ret);
                    goto end_of_glx_get_config;
                }
            }
            break;
        }
    }
    
    if (i < N_MAX_CONFIGS) {
        if (i == nbConfigs) {
            configs[i].visualid = visualid;
            configs[i].nbAttribs = 0;
            int tabGottenValues[N_REQUESTED_ATTRIBS];
            int tabGottenRes[N_REQUESTED_ATTRIBS];
            DEBUGLOG_GL("glXGetConfig_extended visual=%p\n", vis);
            long args[] = {INT_TO_ARG(visualid),
                           INT_TO_ARG(N_REQUESTED_ATTRIBS),
                           POINTER_TO_ARG(getTabRequestedAttribsInt()),
                           POINTER_TO_ARG(tabGottenValues),
                           POINTER_TO_ARG(tabGottenRes)};
            int args_size[] = {0, 0, N_REQUESTED_ATTRIBS * sizeof(int),
                               N_REQUESTED_ATTRIBS * sizeof(int),
                               N_REQUESTED_ATTRIBS * sizeof(int)};
            do_opengl_call_no_lock(glXGetConfig_extended_func, NULL,
                                   CHECK_ARGS(args, args_size));
            
            int j;
            int found = 0;
            for (j = 0; j < N_REQUESTED_ATTRIBS; j++) {
                configs[i].attribs[j].attrib = tabRequestedAttribsPair[j].val;
                configs[i].attribs[j].value = tabGottenValues[j];
                configs[i].attribs[j].ret = tabGottenRes[j];
                configs[i].nbAttribs++;
                if (tabRequestedAttribsPair[j].val == attrib) {
                    found = 1;
                    *value = configs[i].attribs[j].value;
                    ret = configs[i].attribs[j].ret;
                    DEBUGLOG_GL("glXGetConfig(%s)=%d (%d)\n",
                                tabRequestedAttribsPair[j].name, *value, ret);
                }
            }
            
            nbConfigs++;
            if (found) {
                goto end_of_glx_get_config;
            }
        }
        
        long args[] = {INT_TO_ARG(visualid),
                       INT_TO_ARG(attrib), POINTER_TO_ARG(value)};
        do_opengl_call_no_lock(glXGetConfig_func, &ret, args, NULL);
        DEBUGLOG_GL("glXGetConfig visual=%p, attrib=%d -> %d\n",
                    vis, attrib, *value);
        if (configs[i].nbAttribs < N_MAX_ATTRIBS) {
            configs[i].attribs[configs[i].nbAttribs].attrib = attrib;
            configs[i].attribs[configs[i].nbAttribs].value = *value;
            configs[i].attribs[configs[i].nbAttribs].ret = ret;
            configs[i].nbAttribs++;
        }
    } else {
        long args[] = {INT_TO_ARG(visualid), INT_TO_ARG(attrib),
                       POINTER_TO_ARG(value)};
        do_opengl_call_no_lock(glXGetConfig_func, &ret, args, NULL);
        DEBUGLOG_GL("glXGetConfig visual=%p, attrib=%d -> %d\n",
                    vis, attrib, *value);
    }
end_of_glx_get_config:
    UNLOCK(glXGetConfig_func);
    return ret;
}

static void glXSwapBuffers_no_lock(Display *dpy, GLXDrawable drawable)
{
    long args[] = {INT_TO_ARG(drawable)};
    do_opengl_call_no_lock(glXSwapBuffers_func, NULL, args, NULL);
    update_win(dpy, drawable);
}

GLAPI void APIENTRY glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    LOCK(glXSwapBuffers_func);
    glXSwapBuffers_no_lock(dpy, drawable);
    UNLOCK(glXSwapBuffers_func);
}

GLAPI Bool APIENTRY glXQueryExtension(Display *dpy, int *errorBase,
                                      int *eventBase)
{
    if (errorBase) {
        *errorBase = 0;
    }
    if (eventBase) {
        *eventBase = 0;
    }
    return True;
}

GLAPI void APIENTRY glXWaitGL(void)
{
    int ret;
    do_opengl_call(glXWaitGL_func, &ret, NULL, NULL);
}

GLAPI void APIENTRY glXWaitX(void)
{
    // do nothing
}

GLAPI Display * APIENTRY glXGetCurrentDisplay(void)
{
    GET_CURRENT_STATE();
    return state->display;
}

static GLXFBConfig *glXChooseFBConfig_no_lock(Display *dpy, int screen,
                                              const int *attribList,
                                              int *nitems)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI GLXFBConfig * APIENTRY glXChooseFBConfig(Display *dpy, int screen,
                                               const int *attribList,
                                               int *nitems)
{
    //LOCK(glXChooseFBConfig_func);
    GLXFBConfig *fbconfig = glXChooseFBConfig_no_lock(dpy, screen, attribList,
                                                      nitems);
    //UNLOCK(glXChooseFBConfig_func);
    return fbconfig;
}

GLAPI GLXFBConfigSGIX * APIENTRY glXChooseFBConfigSGIX(Display *dpy,
                                                       int screen,
                                                       const int *attribList,
                                                       int *nitems)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI GLXFBConfig * APIENTRY glXGetFBConfigs(Display *dpy, int screen,
                                             int *nitems)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI int APIENTRY glXGetFBConfigAttrib(Display *dpy, GLXFBConfig config,
                                        int attrib, int *value)
{
    //LOCK(glXGetFBConfigAttrib_func);
    NOT_IMPLEMENTED();
    //UNLOCK(glXGetFBConfigAttrib_func);
    return 0;
}

GLAPI int APIENTRY glXGetFBConfigAttribSGIX(Display *dpy,
                                            GLXFBConfigSGIX config,
                                            int attribute, int *value)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI int APIENTRY glXQueryContext(Display *dpy, GLXContext ctx, int attribute,
                                   int *value)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI void APIENTRY glXQueryDrawable(Display *dpy, GLXDrawable draw,
                                     int attribute, unsigned int *value)
{
    NOT_IMPLEMENTED();
}

GLAPI int APIENTRY glXQueryGLXPbufferSGIX(Display *dpy, GLXPbufferSGIX pbuf,
                                          int attribute, unsigned int *value)
{
    NOT_IMPLEMENTED();
    return 0;
}

static GLXPbuffer glXCreatePbuffer_no_lock(Display *dpy,
                                           GLXFBConfig config,
                                           const int *attribList)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI GLXPbuffer APIENTRY glXCreatePbuffer(Display *dpy,
                                           GLXFBConfig config,
                                           const int *attribList)
{
    //LOCK(glXCreatePbuffer_func);
    GLXPbuffer pbuffer = glXCreatePbuffer_no_lock(dpy, config, attribList);
    //UNLOCK(glXCreatePbuffer_func);
    return pbuffer;
}

GLAPI GLXPbufferSGIX APIENTRY glXCreateGLXPbufferSGIX(Display *dpy,
                                                      GLXFBConfigSGIX config,
                                                      unsigned int width,
                                                      unsigned int height,
                                                      int *attribList)
{
    NOT_IMPLEMENTED();
    return 0;
}

void glXBindTexImageATI(Display *dpy, GLXPbuffer pbuffer, int buffer)
{
    NOT_IMPLEMENTED();
}

void glXReleaseTexImageATI(Display *dpy, GLXPbuffer pbuffer, int buffer)
{
    NOT_IMPLEMENTED();
}

Bool glXBindTexImageARB(Display *dpy, GLXPbuffer pbuffer, int buffer)
{
    NOT_IMPLEMENTED();
    return False;
}

Bool glXReleaseTexImageARB(Display *dpy, GLXPbuffer pbuffer, int buffer)
{
    NOT_IMPLEMENTED();
    return False;
}

GLAPI void APIENTRY glXDestroyPbuffer(Display* dpy, GLXPbuffer pbuffer)
{
    DEBUGLOG_GL("glXDestroyPbuffer %d\n", (int)pbuffer);
    long args[] = {INT_TO_ARG(pbuffer)};
    do_opengl_call(glXDestroyPbuffer_func, NULL, args, NULL);
}

GLAPI void APIENTRY glXDestroyGLXPbufferSGIX(Display* dpy,
                                             GLXPbufferSGIX pbuffer)
{
    NOT_IMPLEMENTED();
}

static XVisualInfo *glXGetVisualFromFBConfig_no_lock(Display *dpy,
                                                     GLXFBConfig config)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI XVisualInfo * APIENTRY glXGetVisualFromFBConfig(Display *dpy,
                                                      GLXFBConfig config)
{
    //LOCK(glXGetVisualFromFBConfig_func);
    XVisualInfo *vis = glXGetVisualFromFBConfig_no_lock(dpy, config);
    //UNLOCK(glXGetVisualFromFBConfig_func);
    return vis;
}

GLAPI GLXContext APIENTRY glXCreateNewContext(Display *dpy,
                                              GLXFBConfig fbconfig,
                                              int renderType,
                                              GLXContext shareList,
                                              Bool direct)
{
    //LOCK(glXCreateNewContext_func);
    NOT_IMPLEMENTED();
    //UNLOCK(glXCreateNewContext_func);
    return 0;
}

GLAPI Bool APIENTRY glXMakeContextCurrent(Display *dpy, GLXDrawable draw,
                                          GLXDrawable read, GLXContext ctx)
{
    Bool ret;
    GET_CURRENT_STATE();
    if (draw != read) {
        static int first_time = 1;
        if (first_time) {
            first_time = 0;
            log_gl("using glXMakeCurrent instead of real glXMakeContextCurrent... may help some program work...\n");
        }
    }
    ret = glXMakeCurrent(dpy, draw, ctx);
    if (ret) {
        state->current_read_drawable = read;
    }
    return ret;
}

GLAPI GLXContext APIENTRY glXCreateContextWithConfigSGIX(Display *dpy,
                                                         GLXFBConfigSGIX config,
                                                         int renderType,
                                                         GLXContext shareList,
                                                         Bool direct )
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI GLXWindow APIENTRY glXCreateWindow(Display *dpy, GLXFBConfig config,
                                         Window win, const int *attribList)
{
    /* do nothing. Not sure about this implementation. FIXME */
    NOT_IMPLEMENTED();
    return win;
}

GLAPI void APIENTRY glXDestroyWindow(Display *dpy, GLXWindow window)
{
    /* do nothing. Not sure about this implementation. FIXME */
    NOT_IMPLEMENTED();
}

GLAPI GLXPixmap APIENTRY glXCreateGLXPixmap(Display *dpy,
                                            XVisualInfo *vis,
                                            Pixmap pixmap)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI void APIENTRY glXDestroyGLXPixmap(Display *dpy, GLXPixmap pixmap)
{
    NOT_IMPLEMENTED();
}

GLAPI GLXPixmap APIENTRY glXCreatePixmap(Display *dpy, GLXFBConfig config,
                                         Pixmap pixmap, const int *attribList)
{
    NOT_IMPLEMENTED();
    return 0;
}

GLAPI void APIENTRY glXDestroyPixmap(Display *dpy, GLXPixmap pixmap)
{
    NOT_IMPLEMENTED();
}

GLAPI GLXDrawable APIENTRY glXGetCurrentReadDrawable(void)
{
    GET_CURRENT_STATE();
    return state->current_read_drawable;
}

GLAPI void APIENTRY glXSelectEvent(Display *dpy, GLXDrawable drawable,
                                   unsigned long mask)
{
    NOT_IMPLEMENTED();
}

GLAPI void APIENTRY glXGetSelectedEvent(Display *dpy, GLXDrawable drawable,
                                        unsigned long *mask )
{
    NOT_IMPLEMENTED();
}

#include "opengl_client_xfonts.c"

GLAPI const char * APIENTRY glXGetScreenDriver(Display *dpy, int screen)
{
    //LOCK(glXGetScreenDriver_func);
    NOT_IMPLEMENTED();
    //UNLOCK(glXGetScreenDriver_func);
    return 0;
}

GLAPI const char * APIENTRY glXGetDriverConfig(const char *drivername)
{
    NOT_IMPLEMENTED();
    return 0;
}

static int counterSync = 0;

GLAPI int APIENTRY glXWaitVideoSyncSGI(int divisor, int remainder,
                                       unsigned int *count)
{
    *count = counterSync++; // FIXME ?
    return 0;
}

GLAPI int APIENTRY glXGetVideoSyncSGI(unsigned int *count)
{
    *count = counterSync++; // FIXME ?
    return 0;
}

GLAPI int APIENTRY glXSwapIntervalSGI(int interval)
{
    long args[] = {INT_TO_ARG(interval)};
    int ret = 0;
    do_opengl_call(glXSwapIntervalSGI_func, &ret, args, NULL);
    return ret;
}

static const char* global_glXGetProcAddress_request =
{
"glAccum\0"
"glActiveStencilFaceEXT\0"
"glActiveTexture\0"
"glActiveTextureARB\0"
"glActiveVaryingNV\0"
"glAddSwapHintRectWIN\0"
"glAlphaFragmentOp1ATI\0"
"glAlphaFragmentOp2ATI\0"
"glAlphaFragmentOp3ATI\0"
"glAlphaFunc\0"
"glApplyTextureEXT\0"
"glAreProgramsResidentNV\0"
"glAreTexturesResident\0"
"glAreTexturesResidentEXT\0"
"glArrayElement\0"
"glArrayElementEXT\0"
"glArrayObjectATI\0"
"glAsyncMarkerSGIX\0"
"glAttachObjectARB\0"
"glAttachShader\0"
"glBegin\0"
"glBeginConditionalRenderNVX\0"
"glBeginDefineVisibilityQueryATI\0"
"glBeginFragmentShaderATI\0"
"glBeginOcclusionQuery\0"
"glBeginOcclusionQueryNV\0"
"glBeginQuery\0"
"glBeginQueryARB\0"
"glBeginSceneEXT\0"
"glBeginTransformFeedbackNV\0"
"glBeginUseVisibilityQueryATI\0"
"glBeginVertexShaderEXT\0"
"glBindArraySetARB\0"
"glBindAttribLocation\0"
"glBindAttribLocationARB\0"
"glBindBuffer\0"
"glBindBufferARB\0"
"glBindBufferBaseNV\0"
"glBindBufferOffsetNV\0"
"glBindBufferRangeNV\0"
"glBindFragDataLocationEXT\0"
"glBindFragmentShaderATI\0"
"glBindFramebufferEXT\0"
"glBindLightParameterEXT\0"
"glBindMaterialParameterEXT\0"
"glBindParameterEXT\0"
"glBindProgramARB\0"
"glBindProgramNV\0"
"glBindRenderbufferEXT\0"
"glBindTexGenParameterEXT\0"
"glBindTexture\0"
"glBindTextureEXT\0"
"glBindTextureUnitParameterEXT\0"
"glBindVertexArrayAPPLE\0"
"glBindVertexShaderEXT\0"
"glBinormal3bEXT\0"
"glBinormal3bvEXT\0"
"glBinormal3dEXT\0"
"glBinormal3dvEXT\0"
"glBinormal3fEXT\0"
"glBinormal3fvEXT\0"
"glBinormal3iEXT\0"
"glBinormal3ivEXT\0"
"glBinormal3sEXT\0"
"glBinormal3svEXT\0"
"glBinormalArrayEXT\0"
"glBitmap\0"
"glBlendColor\0"
"glBlendColorEXT\0"
"glBlendEquation\0"
"glBlendEquationEXT\0"
"glBlendEquationSeparate\0"
"glBlendEquationSeparateATI\0"
"glBlendEquationSeparateEXT\0"
"glBlendFunc\0"
"glBlendFuncSeparate\0"
"glBlendFuncSeparateEXT\0"
"glBlendFuncSeparateINGR\0"
"glBlitFramebufferEXT\0"
"glBufferData\0"
"glBufferDataARB\0"
"glBufferParameteriAPPLE\0"
"glBufferRegionEnabled\0"
"glBufferRegionEnabledEXT\0"
"glBufferSubData\0"
"glBufferSubDataARB\0"
"glCallList\0"
"glCallLists\0"
"glCheckFramebufferStatusEXT\0"
"glClampColorARB\0"
"glClear\0"
"glClearAccum\0"
"glClearColor\0"
"glClearColorIiEXT\0"
"glClearColorIuiEXT\0"
"glClearDepth\0"
"glClearDepthdNV\0"
"glClearIndex\0"
"glClearStencil\0"
"glClientActiveTexture\0"
"glClientActiveTextureARB\0"
"glClientActiveVertexStreamATI\0"
"glClipPlane\0"
"glColor3b\0"
"glColor3bv\0"
"glColor3d\0"
"glColor3dv\0"
"glColor3f\0"
"glColor3fv\0"
"glColor3fVertex3fSUN\0"
"glColor3fVertex3fvSUN\0"
"glColor3hNV\0"
"glColor3hvNV\0"
"glColor3i\0"
"glColor3iv\0"
"glColor3s\0"
"glColor3sv\0"
"glColor3ub\0"
"glColor3ubv\0"
"glColor3ui\0"
"glColor3uiv\0"
"glColor3us\0"
"glColor3usv\0"
"glColor4b\0"
"glColor4bv\0"
"glColor4d\0"
"glColor4dv\0"
"glColor4f\0"
"glColor4fNormal3fVertex3fSUN\0"
"glColor4fNormal3fVertex3fvSUN\0"
"glColor4fv\0"
"glColor4hNV\0"
"glColor4hvNV\0"
"glColor4i\0"
"glColor4iv\0"
"glColor4s\0"
"glColor4sv\0"
"glColor4ub\0"
"glColor4ubv\0"
"glColor4ubVertex2fSUN\0"
"glColor4ubVertex2fvSUN\0"
"glColor4ubVertex3fSUN\0"
"glColor4ubVertex3fvSUN\0"
"glColor4ui\0"
"glColor4uiv\0"
"glColor4us\0"
"glColor4usv\0"
"glColorFragmentOp1ATI\0"
"glColorFragmentOp2ATI\0"
"glColorFragmentOp3ATI\0"
"glColorMask\0"
"glColorMaskIndexedEXT\0"
"glColorMaterial\0"
"glColorPointer\0"
"glColorPointerEXT\0"
"glColorPointerListIBM\0"
"glColorPointervINTEL\0"
"glColorSubTable\0"
"glColorSubTableEXT\0"
"glColorTable\0"
"glColorTableEXT\0"
"glColorTableParameterfv\0"
"glColorTableParameterfvSGI\0"
"glColorTableParameteriv\0"
"glColorTableParameterivSGI\0"
"glColorTableSGI\0"
"glCombinerInputNV\0"
"glCombinerOutputNV\0"
"glCombinerParameterfNV\0"
"glCombinerParameterfvNV\0"
"glCombinerParameteriNV\0"
"glCombinerParameterivNV\0"
"glCombinerStageParameterfvNV\0"
"glCompileShader\0"
"glCompileShaderARB\0"
"glCompressedTexImage1D\0"
"glCompressedTexImage1DARB\0"
"glCompressedTexImage2D\0"
"glCompressedTexImage2DARB\0"
"glCompressedTexImage3D\0"
"glCompressedTexImage3DARB\0"
"glCompressedTexSubImage1D\0"
"glCompressedTexSubImage1DARB\0"
"glCompressedTexSubImage2D\0"
"glCompressedTexSubImage2DARB\0"
"glCompressedTexSubImage3D\0"
"glCompressedTexSubImage3DARB\0"
"glConvolutionFilter1D\0"
"glConvolutionFilter1DEXT\0"
"glConvolutionFilter2D\0"
"glConvolutionFilter2DEXT\0"
"glConvolutionParameterf\0"
"glConvolutionParameterfEXT\0"
"glConvolutionParameterfv\0"
"glConvolutionParameterfvEXT\0"
"glConvolutionParameteri\0"
"glConvolutionParameteriEXT\0"
"glConvolutionParameteriv\0"
"glConvolutionParameterivEXT\0"
"glCopyColorSubTable\0"
"glCopyColorSubTableEXT\0"
"glCopyColorTable\0"
"glCopyColorTableSGI\0"
"glCopyConvolutionFilter1D\0"
"glCopyConvolutionFilter1DEXT\0"
"glCopyConvolutionFilter2D\0"
"glCopyConvolutionFilter2DEXT\0"
"glCopyPixels\0"
"glCopyTexImage1D\0"
"glCopyTexImage1DEXT\0"
"glCopyTexImage2D\0"
"glCopyTexImage2DEXT\0"
"glCopyTexSubImage1D\0"
"glCopyTexSubImage1DEXT\0"
"glCopyTexSubImage2D\0"
"glCopyTexSubImage2DEXT\0"
"glCopyTexSubImage3D\0"
"glCopyTexSubImage3DEXT\0"
"glCreateProgram\0"
"glCreateProgramObjectARB\0"
"glCreateShader\0"
"glCreateShaderObjectARB\0"
"glCullFace\0"
"glCullParameterdvEXT\0"
"glCullParameterfvEXT\0"
"glCurrentPaletteMatrixARB\0"
"glDeformationMap3dSGIX\0"
"glDeformationMap3fSGIX\0"
"glDeformSGIX\0"
"glDeleteArraySetsARB\0"
"glDeleteAsyncMarkersSGIX\0"
"glDeleteBufferRegion\0"
"glDeleteBufferRegionEXT\0"
"glDeleteBuffers\0"
"glDeleteBuffersARB\0"
"glDeleteFencesAPPLE\0"
"glDeleteFencesNV\0"
"glDeleteFragmentShaderATI\0"
"glDeleteFramebuffersEXT\0"
"glDeleteLists\0"
"glDeleteObjectARB\0"
"glDeleteObjectBufferATI\0"
"glDeleteOcclusionQueries\0"
"glDeleteOcclusionQueriesNV\0"
"glDeleteProgram\0"
"glDeleteProgramsARB\0"
"glDeleteProgramsNV\0"
"glDeleteQueries\0"
"glDeleteQueriesARB\0"
"glDeleteRenderbuffersEXT\0"
"glDeleteShader\0"
"glDeleteTextures\0"
"glDeleteTexturesEXT\0"
"glDeleteVertexArraysAPPLE\0"
"glDeleteVertexShaderEXT\0"
"glDeleteVisibilityQueriesATI\0"
"glDepthBoundsdNV\0"
"glDepthBoundsEXT\0"
"glDepthFunc\0"
"glDepthMask\0"
"glDepthRange\0"
"glDepthRangedNV\0"
"glDetachObjectARB\0"
"glDetachShader\0"
"glDetailTexFuncSGIS\0"
"glDisable\0"
"glDisableClientState\0"
"glDisableIndexedEXT\0"
"glDisableVariantClientStateEXT\0"
"glDisableVertexAttribAPPLE\0"
"glDisableVertexAttribArray\0"
"glDisableVertexAttribArrayARB\0"
"glDrawArrays\0"
"glDrawArraysEXT\0"
"glDrawArraysInstancedEXT\0"
"glDrawBuffer\0"
"glDrawBufferRegion\0"
"glDrawBufferRegionEXT\0"
"glDrawBuffers\0"
"glDrawBuffersARB\0"
"glDrawBuffersATI\0"
"glDrawElementArrayAPPLE\0"
"glDrawElementArrayATI\0"
"glDrawElements\0"
"glDrawElementsFGL\0"
"glDrawElementsInstancedEXT\0"
"glDrawMeshArraysSUN\0"
"glDrawMeshNV\0"
"glDrawPixels\0"
"glDrawRangeElementArrayAPPLE\0"
"glDrawRangeElementArrayATI\0"
"glDrawRangeElements\0"
"glDrawRangeElementsEXT\0"
"glDrawWireTrianglesFGL\0"
"glEdgeFlag\0"
"glEdgeFlagPointer\0"
"glEdgeFlagPointerEXT\0"
"glEdgeFlagPointerListIBM\0"
"glEdgeFlagv\0"
"glElementPointerAPPLE\0"
"glElementPointerATI\0"
"glEnable\0"
"glEnableClientState\0"
"glEnableIndexedEXT\0"
"glEnableVariantClientStateEXT\0"
"glEnableVertexAttribAPPLE\0"
"glEnableVertexAttribArray\0"
"glEnableVertexAttribArrayARB\0"
"glEnd\0"
"glEndConditionalRenderNVX\0"
"glEndDefineVisibilityQueryATI\0"
"glEndFragmentShaderATI\0"
"glEndList\0"
"glEndOcclusionQuery\0"
"glEndOcclusionQueryNV\0"
"glEndQuery\0"
"glEndQueryARB\0"
"glEndSceneEXT\0"
"glEndTransformFeedbackNV\0"
"glEndUseVisibilityQueryATI\0"
"glEndVertexShaderEXT\0"
"glEvalCoord1d\0"
"glEvalCoord1dv\0"
"glEvalCoord1f\0"
"glEvalCoord1fv\0"
"glEvalCoord2d\0"
"glEvalCoord2dv\0"
"glEvalCoord2f\0"
"glEvalCoord2fv\0"
"glEvalMapsNV\0"
"glEvalMesh1\0"
"glEvalMesh2\0"
"glEvalPoint1\0"
"glEvalPoint2\0"
"glExecuteProgramNV\0"
"glExtractComponentEXT\0"
"glFeedbackBuffer\0"
"glFinalCombinerInputNV\0"
"glFinish\0"
"glFinishAsyncSGIX\0"
"glFinishFenceAPPLE\0"
"glFinishFenceNV\0"
"glFinishObjectAPPLE\0"
"glFinishRenderAPPLE\0"
"glFinishTextureSUNX\0"
"glFlush\0"
"glFlushMappedBufferRangeAPPLE\0"
"glFlushPixelDataRangeNV\0"
"glFlushRasterSGIX\0"
"glFlushRenderAPPLE\0"
"glFlushVertexArrayRangeAPPLE\0"
"glFlushVertexArrayRangeNV\0"
"glFogCoordd\0"
"glFogCoorddEXT\0"
"glFogCoorddv\0"
"glFogCoorddvEXT\0"
"glFogCoordf\0"
"glFogCoordfEXT\0"
"glFogCoordfv\0"
"glFogCoordfvEXT\0"
"glFogCoordhNV\0"
"glFogCoordhvNV\0"
"glFogCoordPointer\0"
"glFogCoordPointerEXT\0"
"glFogCoordPointerListIBM\0"
"glFogf\0"
"glFogFuncSGIS\0"
"glFogfv\0"
"glFogi\0"
"glFogiv\0"
"glFragmentColorMaterialEXT\0"
"glFragmentColorMaterialSGIX\0"
"glFragmentLightfEXT\0"
"glFragmentLightfSGIX\0"
"glFragmentLightfvEXT\0"
"glFragmentLightfvSGIX\0"
"glFragmentLightiEXT\0"
"glFragmentLightiSGIX\0"
"glFragmentLightivEXT\0"
"glFragmentLightivSGIX\0"
"glFragmentLightModelfEXT\0"
"glFragmentLightModelfSGIX\0"
"glFragmentLightModelfvEXT\0"
"glFragmentLightModelfvSGIX\0"
"glFragmentLightModeliEXT\0"
"glFragmentLightModeliSGIX\0"
"glFragmentLightModelivEXT\0"
"glFragmentLightModelivSGIX\0"
"glFragmentMaterialfEXT\0"
"glFragmentMaterialfSGIX\0"
"glFragmentMaterialfvEXT\0"
"glFragmentMaterialfvSGIX\0"
"glFragmentMaterialiEXT\0"
"glFragmentMaterialiSGIX\0"
"glFragmentMaterialivEXT\0"
"glFragmentMaterialivSGIX\0"
"glFramebufferRenderbufferEXT\0"
"glFramebufferTexture1DEXT\0"
"glFramebufferTexture2DEXT\0"
"glFramebufferTexture3DEXT\0"
"glFramebufferTextureEXT\0"
"glFramebufferTextureFaceEXT\0"
"glFramebufferTextureLayerEXT\0"
"glFrameZoomSGIX\0"
"glFreeObjectBufferATI\0"
"glFrontFace\0"
"glFrustum\0"
"glGenArraySetsARB\0"
"glGenAsyncMarkersSGIX\0"
"glGenBuffers\0"
"glGenBuffersARB\0"
"glGenerateMipmapEXT\0"
"glGenFencesAPPLE\0"
"glGenFencesNV\0"
"glGenFragmentShadersATI\0"
"glGenFramebuffersEXT\0"
"glGenLists\0"
"glGenOcclusionQueries\0"
"glGenOcclusionQueriesNV\0"
"glGenProgramsARB\0"
"glGenProgramsNV\0"
"glGenQueries\0"
"glGenQueriesARB\0"
"glGenRenderbuffersEXT\0"
"glGenSymbolsEXT\0"
"glGenTextures\0"
"glGenTexturesEXT\0"
"glGenVertexArraysAPPLE\0"
"glGenVertexShadersEXT\0"
"glGenVisibilityQueriesATI\0"
"glGetActiveAttrib\0"
"glGetActiveAttribARB\0"
"glGetActiveUniform\0"
"glGetActiveUniformARB\0"
"glGetActiveVaryingNV\0"
"glGetArrayObjectfvATI\0"
"glGetArrayObjectivATI\0"
"glGetAttachedObjectsARB\0"
"glGetAttachedShaders\0"
"glGetAttribLocation\0"
"glGetAttribLocationARB\0"
"glGetBooleanIndexedvEXT\0"
"glGetBooleanv\0"
"glGetBufferParameteriv\0"
"glGetBufferParameterivARB\0"
"glGetBufferPointerv\0"
"glGetBufferPointervARB\0"
"glGetBufferSubData\0"
"glGetBufferSubDataARB\0"
"glGetClipPlane\0"
"glGetColorTable\0"
"glGetColorTableEXT\0"
"glGetColorTableParameterfv\0"
"glGetColorTableParameterfvEXT\0"
"glGetColorTableParameterfvSGI\0"
"glGetColorTableParameteriv\0"
"glGetColorTableParameterivEXT\0"
"glGetColorTableParameterivSGI\0"
"glGetColorTableSGI\0"
"glGetCombinerInputParameterfvNV\0"
"glGetCombinerInputParameterivNV\0"
"glGetCombinerOutputParameterfvNV\0"
"glGetCombinerOutputParameterivNV\0"
"glGetCombinerStageParameterfvNV\0"
"glGetCompressedTexImage\0"
"glGetCompressedTexImageARB\0"
"glGetConvolutionFilter\0"
"glGetConvolutionFilterEXT\0"
"glGetConvolutionParameterfv\0"
"glGetConvolutionParameterfvEXT\0"
"glGetConvolutionParameteriv\0"
"glGetConvolutionParameterivEXT\0"
"glGetDetailTexFuncSGIS\0"
"glGetDoublev\0"
"glGetError\0"
"glGetFenceivNV\0"
"glGetFinalCombinerInputParameterfvNV\0"
"glGetFinalCombinerInputParameterivNV\0"
"glGetFloatv\0"
"glGetFogFuncSGIS\0"
"glGetFragDataLocationEXT\0"
"glGetFragmentLightfvEXT\0"
"glGetFragmentLightfvSGIX\0"
"glGetFragmentLightivEXT\0"
"glGetFragmentLightivSGIX\0"
"glGetFragmentMaterialfvEXT\0"
"glGetFragmentMaterialfvSGIX\0"
"glGetFragmentMaterialivEXT\0"
"glGetFragmentMaterialivSGIX\0"
"glGetFramebufferAttachmentParameterivEXT\0"
"glGetHandleARB\0"
"glGetHistogram\0"
"glGetHistogramEXT\0"
"glGetHistogramParameterfv\0"
"glGetHistogramParameterfvEXT\0"
"glGetHistogramParameteriv\0"
"glGetHistogramParameterivEXT\0"
"glGetImageTransformParameterfvHP\0"
"glGetImageTransformParameterivHP\0"
"glGetInfoLogARB\0"
"glGetInstrumentsSGIX\0"
"glGetIntegerIndexedvEXT\0"
"glGetIntegerv\0"
"glGetInvariantBooleanvEXT\0"
"glGetInvariantFloatvEXT\0"
"glGetInvariantIntegervEXT\0"
"glGetLightfv\0"
"glGetLightiv\0"
"glGetListParameterfvSGIX\0"
"glGetListParameterivSGIX\0"
"glGetLocalConstantBooleanvEXT\0"
"glGetLocalConstantFloatvEXT\0"
"glGetLocalConstantIntegervEXT\0"
"glGetMapAttribParameterfvNV\0"
"glGetMapAttribParameterivNV\0"
"glGetMapControlPointsNV\0"
"glGetMapdv\0"
"glGetMapfv\0"
"glGetMapiv\0"
"glGetMapParameterfvNV\0"
"glGetMapParameterivNV\0"
"glGetMaterialfv\0"
"glGetMaterialiv\0"
"glGetMinmax\0"
"glGetMinmaxEXT\0"
"glGetMinmaxParameterfv\0"
"glGetMinmaxParameterfvEXT\0"
"glGetMinmaxParameteriv\0"
"glGetMinmaxParameterivEXT\0"
"glGetObjectBufferfvATI\0"
"glGetObjectBufferivATI\0"
"glGetObjectParameterfvARB\0"
"glGetObjectParameterivARB\0"
"glGetOcclusionQueryiv\0"
"glGetOcclusionQueryivNV\0"
"glGetOcclusionQueryuiv\0"
"glGetOcclusionQueryuivNV\0"
"glGetPixelMapfv\0"
"glGetPixelMapuiv\0"
"glGetPixelMapusv\0"
"glGetPixelTexGenParameterfvSGIS\0"
"glGetPixelTexGenParameterivSGIS\0"
"glGetPixelTransformParameterfvEXT\0"
"glGetPixelTransformParameterivEXT\0"
"glGetPointerv\0"
"glGetPointervEXT\0"
"glGetPolygonStipple\0"
"glGetProgramEnvParameterdvARB\0"
"glGetProgramEnvParameterfvARB\0"
"glGetProgramEnvParameterIivNV\0"
"glGetProgramEnvParameterIuivNV\0"
"glGetProgramInfoLog\0"
"glGetProgramiv\0"
"glGetProgramivARB\0"
"glGetProgramivNV\0"
"glGetProgramLocalParameterdvARB\0"
"glGetProgramLocalParameterfvARB\0"
"glGetProgramLocalParameterIivNV\0"
"glGetProgramLocalParameterIuivNV\0"
"glGetProgramNamedParameterdvNV\0"
"glGetProgramNamedParameterfvNV\0"
"glGetProgramParameterdvNV\0"
"glGetProgramParameterfvNV\0"
"glGetProgramRegisterfvMESA\0"
"glGetProgramStringARB\0"
"glGetProgramStringNV\0"
"glGetQueryiv\0"
"glGetQueryivARB\0"
"glGetQueryObjecti64vEXT\0"
"glGetQueryObjectiv\0"
"glGetQueryObjectivARB\0"
"glGetQueryObjectui64vEXT\0"
"glGetQueryObjectuiv\0"
"glGetQueryObjectuivARB\0"
"glGetRenderbufferParameterivEXT\0"
"glGetSeparableFilter\0"
"glGetSeparableFilterEXT\0"
"glGetShaderInfoLog\0"
"glGetShaderiv\0"
"glGetShaderSource\0"
"glGetShaderSourceARB\0"
"glGetSharpenTexFuncSGIS\0"
"glGetString\0"
"glGetTexBumpParameterfvATI\0"
"glGetTexBumpParameterivATI\0"
"glGetTexEnvfv\0"
"glGetTexEnviv\0"
"glGetTexFilterFuncSGIS\0"
"glGetTexGendv\0"
"glGetTexGenfv\0"
"glGetTexGeniv\0"
"glGetTexImage\0"
"glGetTexLevelParameterfv\0"
"glGetTexLevelParameteriv\0"
"glGetTexParameterfv\0"
"glGetTexParameterIivEXT\0"
"glGetTexParameterIuivEXT\0"
"glGetTexParameteriv\0"
"glGetTexParameterPointervAPPLE\0"
"glGetTrackMatrixivNV\0"
"glGetTransformFeedbackVaryingNV\0"
"glGetUniformBufferSizeEXT\0"
"glGetUniformfv\0"
"glGetUniformfvARB\0"
"glGetUniformiv\0"
"glGetUniformivARB\0"
"glGetUniformLocation\0"
"glGetUniformLocationARB\0"
"glGetUniformOffsetEXT\0"
"glGetUniformuivEXT\0"
"glGetVariantArrayObjectfvATI\0"
"glGetVariantArrayObjectivATI\0"
"glGetVariantBooleanvEXT\0"
"glGetVariantFloatvEXT\0"
"glGetVariantIntegervEXT\0"
"glGetVariantPointervEXT\0"
"glGetVaryingLocationNV\0"
"glGetVertexAttribArrayObjectfvATI\0"
"glGetVertexAttribArrayObjectivATI\0"
"glGetVertexAttribdv\0"
"glGetVertexAttribdvARB\0"
"glGetVertexAttribdvNV\0"
"glGetVertexAttribfv\0"
"glGetVertexAttribfvARB\0"
"glGetVertexAttribfvNV\0"
"glGetVertexAttribIivEXT\0"
"glGetVertexAttribIuivEXT\0"
"glGetVertexAttribiv\0"
"glGetVertexAttribivARB\0"
"glGetVertexAttribivNV\0"
"glGetVertexAttribPointerv\0"
"glGetVertexAttribPointervARB\0"
"glGetVertexAttribPointervNV\0"
"glGlobalAlphaFactorbSUN\0"
"glGlobalAlphaFactordSUN\0"
"glGlobalAlphaFactorfSUN\0"
"glGlobalAlphaFactoriSUN\0"
"glGlobalAlphaFactorsSUN\0"
"glGlobalAlphaFactorubSUN\0"
"glGlobalAlphaFactoruiSUN\0"
"glGlobalAlphaFactorusSUN\0"
"glHint\0"
"glHintPGI\0"
"glHistogram\0"
"glHistogramEXT\0"
"glIglooInterfaceSGIX\0"
"glImageTransformParameterfHP\0"
"glImageTransformParameterfvHP\0"
"glImageTransformParameteriHP\0"
"glImageTransformParameterivHP\0"
"glIndexd\0"
"glIndexdv\0"
"glIndexf\0"
"glIndexFuncEXT\0"
"glIndexfv\0"
"glIndexi\0"
"glIndexiv\0"
"glIndexMask\0"
"glIndexMaterialEXT\0"
"glIndexPointer\0"
"glIndexPointerEXT\0"
"glIndexPointerListIBM\0"
"glIndexs\0"
"glIndexsv\0"
"glIndexub\0"
"glIndexubv\0"
"glInitNames\0"
"glInsertComponentEXT\0"
"glInstrumentsBufferSGIX\0"
"glInterleavedArrays\0"
"glIsArraySetARB\0"
"glIsAsyncMarkerSGIX\0"
"glIsBuffer\0"
"glIsBufferARB\0"
"glIsEnabled\0"
"glIsEnabledIndexedEXT\0"
"glIsFenceAPPLE\0"
"glIsFenceNV\0"
"glIsFramebufferEXT\0"
"glIsList\0"
"glIsObjectBufferATI\0"
"glIsOcclusionQuery\0"
"_glIsOcclusionQueryNV\0"
"glIsOcclusionQueryNV\0"
"glIsProgram\0"
"glIsProgramARB\0"
"glIsProgramNV\0"
"glIsQuery\0"
"glIsQueryARB\0"
"glIsRenderbufferEXT\0"
"glIsShader\0"
"glIsTexture\0"
"glIsTextureEXT\0"
"glIsVariantEnabledEXT\0"
"glIsVertexArrayAPPLE\0"
"glIsVertexAttribEnabledAPPLE\0"
"glLightEnviEXT\0"
"glLightEnviSGIX\0"
"glLightf\0"
"glLightfv\0"
"glLighti\0"
"glLightiv\0"
"glLightModelf\0"
"glLightModelfv\0"
"glLightModeli\0"
"glLightModeliv\0"
"glLineStipple\0"
"glLineWidth\0"
"glLinkProgram\0"
"glLinkProgramARB\0"
"glListBase\0"
"glListParameterfSGIX\0"
"glListParameterfvSGIX\0"
"glListParameteriSGIX\0"
"glListParameterivSGIX\0"
"glLoadIdentity\0"
"glLoadIdentityDeformationMapSGIX\0"
"glLoadMatrixd\0"
"glLoadMatrixf\0"
"glLoadName\0"
"glLoadProgramNV\0"
"glLoadTransposeMatrixd\0"
"glLoadTransposeMatrixdARB\0"
"glLoadTransposeMatrixf\0"
"glLoadTransposeMatrixfARB\0"
"glLockArraysEXT\0"
"glLogicOp\0"
"glMap1d\0"
"glMap1f\0"
"glMap2d\0"
"glMap2f\0"
"glMapBuffer\0"
"glMapBufferARB\0"
"glMapControlPointsNV\0"
"glMapGrid1d\0"
"glMapGrid1f\0"
"glMapGrid2d\0"
"glMapGrid2f\0"
"glMapObjectBufferATI\0"
"glMapParameterfvNV\0"
"glMapParameterivNV\0"
"glMapTexture3DATI\0"
"glMapVertexAttrib1dAPPLE\0"
"glMapVertexAttrib1fAPPLE\0"
"glMapVertexAttrib2dAPPLE\0"
"glMapVertexAttrib2fAPPLE\0"
"glMaterialf\0"
"glMaterialfv\0"
"glMateriali\0"
"glMaterialiv\0"
"glMatrixIndexPointerARB\0"
"glMatrixIndexubvARB\0"
"glMatrixIndexuivARB\0"
"glMatrixIndexusvARB\0"
"glMatrixMode\0"
"glMinmax\0"
"glMinmaxEXT\0"
"glMultiDrawArrays\0"
"glMultiDrawArraysEXT\0"
"glMultiDrawElementArrayAPPLE\0"
"glMultiDrawElements\0"
"glMultiDrawElementsEXT\0"
"glMultiDrawRangeElementArrayAPPLE\0"
"glMultiModeDrawArraysIBM\0"
"glMultiModeDrawElementsIBM\0"
"glMultiTexCoord1d\0"
"glMultiTexCoord1dARB\0"
"glMultiTexCoord1dSGIS\0"
"glMultiTexCoord1dv\0"
"glMultiTexCoord1dvARB\0"
"glMultiTexCoord1dvSGIS\0"
"glMultiTexCoord1f\0"
"glMultiTexCoord1fARB\0"
"glMultiTexCoord1fSGIS\0"
"glMultiTexCoord1fv\0"
"glMultiTexCoord1fvARB\0"
"glMultiTexCoord1fvSGIS\0"
"glMultiTexCoord1hNV\0"
"glMultiTexCoord1hvNV\0"
"glMultiTexCoord1i\0"
"glMultiTexCoord1iARB\0"
"glMultiTexCoord1iSGIS\0"
"glMultiTexCoord1iv\0"
"glMultiTexCoord1ivARB\0"
"glMultiTexCoord1ivSGIS\0"
"glMultiTexCoord1s\0"
"glMultiTexCoord1sARB\0"
"glMultiTexCoord1sSGIS\0"
"glMultiTexCoord1sv\0"
"glMultiTexCoord1svARB\0"
"glMultiTexCoord1svSGIS\0"
"glMultiTexCoord2d\0"
"glMultiTexCoord2dARB\0"
"glMultiTexCoord2dSGIS\0"
"glMultiTexCoord2dv\0"
"glMultiTexCoord2dvARB\0"
"glMultiTexCoord2dvSGIS\0"
"glMultiTexCoord2f\0"
"glMultiTexCoord2fARB\0"
"glMultiTexCoord2fSGIS\0"
"glMultiTexCoord2fv\0"
"glMultiTexCoord2fvARB\0"
"glMultiTexCoord2fvSGIS\0"
"glMultiTexCoord2hNV\0"
"glMultiTexCoord2hvNV\0"
"glMultiTexCoord2i\0"
"glMultiTexCoord2iARB\0"
"glMultiTexCoord2iSGIS\0"
"glMultiTexCoord2iv\0"
"glMultiTexCoord2ivARB\0"
"glMultiTexCoord2ivSGIS\0"
"glMultiTexCoord2s\0"
"glMultiTexCoord2sARB\0"
"glMultiTexCoord2sSGIS\0"
"glMultiTexCoord2sv\0"
"glMultiTexCoord2svARB\0"
"glMultiTexCoord2svSGIS\0"
"glMultiTexCoord3d\0"
"glMultiTexCoord3dARB\0"
"glMultiTexCoord3dSGIS\0"
"glMultiTexCoord3dv\0"
"glMultiTexCoord3dvARB\0"
"glMultiTexCoord3dvSGIS\0"
"glMultiTexCoord3f\0"
"glMultiTexCoord3fARB\0"
"glMultiTexCoord3fSGIS\0"
"glMultiTexCoord3fv\0"
"glMultiTexCoord3fvARB\0"
"glMultiTexCoord3fvSGIS\0"
"glMultiTexCoord3hNV\0"
"glMultiTexCoord3hvNV\0"
"glMultiTexCoord3i\0"
"glMultiTexCoord3iARB\0"
"glMultiTexCoord3iSGIS\0"
"glMultiTexCoord3iv\0"
"glMultiTexCoord3ivARB\0"
"glMultiTexCoord3ivSGIS\0"
"glMultiTexCoord3s\0"
"glMultiTexCoord3sARB\0"
"glMultiTexCoord3sSGIS\0"
"glMultiTexCoord3sv\0"
"glMultiTexCoord3svARB\0"
"glMultiTexCoord3svSGIS\0"
"glMultiTexCoord4d\0"
"glMultiTexCoord4dARB\0"
"glMultiTexCoord4dSGIS\0"
"glMultiTexCoord4dv\0"
"glMultiTexCoord4dvARB\0"
"glMultiTexCoord4dvSGIS\0"
"glMultiTexCoord4f\0"
"glMultiTexCoord4fARB\0"
"glMultiTexCoord4fSGIS\0"
"glMultiTexCoord4fv\0"
"glMultiTexCoord4fvARB\0"
"glMultiTexCoord4fvSGIS\0"
"glMultiTexCoord4hNV\0"
"glMultiTexCoord4hvNV\0"
"glMultiTexCoord4i\0"
"glMultiTexCoord4iARB\0"
"glMultiTexCoord4iSGIS\0"
"glMultiTexCoord4iv\0"
"glMultiTexCoord4ivARB\0"
"glMultiTexCoord4ivSGIS\0"
"glMultiTexCoord4s\0"
"glMultiTexCoord4sARB\0"
"glMultiTexCoord4sSGIS\0"
"glMultiTexCoord4sv\0"
"glMultiTexCoord4svARB\0"
"glMultiTexCoord4svSGIS\0"
"glMultiTexCoordPointerSGIS\0"
"glMultMatrixd\0"
"glMultMatrixf\0"
"glMultTransposeMatrixd\0"
"glMultTransposeMatrixdARB\0"
"glMultTransposeMatrixf\0"
"glMultTransposeMatrixfARB\0"
"glNewBufferRegion\0"
"glNewBufferRegionEXT\0"
"glNewList\0"
"glNewObjectBufferATI\0"
"glNormal3b\0"
"glNormal3bv\0"
"glNormal3d\0"
"glNormal3dv\0"
"glNormal3f\0"
"glNormal3fv\0"
"glNormal3fVertex3fSUN\0"
"glNormal3fVertex3fvSUN\0"
"glNormal3hNV\0"
"glNormal3hvNV\0"
"glNormal3i\0"
"glNormal3iv\0"
"glNormal3s\0"
"glNormal3sv\0"
"glNormalPointer\0"
"glNormalPointerEXT\0"
"glNormalPointerListIBM\0"
"glNormalPointervINTEL\0"
"glNormalStream3bATI\0"
"glNormalStream3bvATI\0"
"glNormalStream3dATI\0"
"glNormalStream3dvATI\0"
"glNormalStream3fATI\0"
"glNormalStream3fvATI\0"
"glNormalStream3iATI\0"
"glNormalStream3ivATI\0"
"glNormalStream3sATI\0"
"glNormalStream3svATI\0"
"glOrtho\0"
"glPassTexCoordATI\0"
"glPassThrough\0"
"glPixelDataRangeNV\0"
"glPixelMapfv\0"
"glPixelMapuiv\0"
"glPixelMapusv\0"
"glPixelStoref\0"
"glPixelStorei\0"
"glPixelTexGenParameterfSGIS\0"
"glPixelTexGenParameterfvSGIS\0"
"glPixelTexGenParameteriSGIS\0"
"glPixelTexGenParameterivSGIS\0"
"glPixelTexGenSGIX\0"
"glPixelTransferf\0"
"glPixelTransferi\0"
"glPixelTransformParameterfEXT\0"
"glPixelTransformParameterfvEXT\0"
"glPixelTransformParameteriEXT\0"
"glPixelTransformParameterivEXT\0"
"glPixelZoom\0"
"glPNTrianglesfATI\0"
"glPNTrianglesiATI\0"
"glPointParameterf\0"
"glPointParameterfARB\0"
"glPointParameterfEXT\0"
"glPointParameterfSGIS\0"
"glPointParameterfv\0"
"glPointParameterfvARB\0"
"glPointParameterfvEXT\0"
"glPointParameterfvSGIS\0"
"glPointParameteri\0"
"glPointParameteriEXT\0"
"glPointParameteriNV\0"
"glPointParameteriv\0"
"glPointParameterivEXT\0"
"glPointParameterivNV\0"
"glPointSize\0"
"glPollAsyncSGIX\0"
"glPollInstrumentsSGIX\0"
"glPolygonMode\0"
"glPolygonOffset\0"
"glPolygonOffsetEXT\0"
"glPolygonStipple\0"
"glPopAttrib\0"
"glPopClientAttrib\0"
"glPopMatrix\0"
"glPopName\0"
"glPrimitiveRestartIndexNV\0"
"glPrimitiveRestartNV\0"
"glPrioritizeTextures\0"
"glPrioritizeTexturesEXT\0"
"glProgramBufferParametersfvNV\0"
"glProgramBufferParametersIivNV\0"
"glProgramBufferParametersIuivNV\0"
"glProgramCallbackMESA\0"
"glProgramEnvParameter4dARB\0"
"glProgramEnvParameter4dvARB\0"
"glProgramEnvParameter4fARB\0"
"glProgramEnvParameter4fvARB\0"
"glProgramEnvParameterI4iNV\0"
"glProgramEnvParameterI4ivNV\0"
"glProgramEnvParameterI4uiNV\0"
"glProgramEnvParameterI4uivNV\0"
"glProgramEnvParameters4fvEXT\0"
"glProgramEnvParametersI4ivNV\0"
"glProgramEnvParametersI4uivNV\0"
"glProgramLocalParameter4dARB\0"
"glProgramLocalParameter4dvARB\0"
"glProgramLocalParameter4fARB\0"
"glProgramLocalParameter4fvARB\0"
"glProgramLocalParameterI4iNV\0"
"glProgramLocalParameterI4ivNV\0"
"glProgramLocalParameterI4uiNV\0"
"glProgramLocalParameterI4uivNV\0"
"glProgramLocalParameters4fvEXT\0"
"glProgramLocalParametersI4ivNV\0"
"glProgramLocalParametersI4uivNV\0"
"glProgramNamedParameter4dNV\0"
"glProgramNamedParameter4dvNV\0"
"glProgramNamedParameter4fNV\0"
"glProgramNamedParameter4fvNV\0"
"glProgramParameter4dNV\0"
"glProgramParameter4dvNV\0"
"glProgramParameter4fNV\0"
"glProgramParameter4fvNV\0"
"glProgramParameteriEXT\0"
"glProgramParameters4dvNV\0"
"glProgramParameters4fvNV\0"
"glProgramStringARB\0"
"glProgramVertexLimitNV\0"
"glPushAttrib\0"
"glPushClientAttrib\0"
"glPushMatrix\0"
"glPushName\0"
"glRasterPos2d\0"
"glRasterPos2dv\0"
"glRasterPos2f\0"
"glRasterPos2fv\0"
"glRasterPos2i\0"
"glRasterPos2iv\0"
"glRasterPos2s\0"
"glRasterPos2sv\0"
"glRasterPos3d\0"
"glRasterPos3dv\0"
"glRasterPos3f\0"
"glRasterPos3fv\0"
"glRasterPos3i\0"
"glRasterPos3iv\0"
"glRasterPos3s\0"
"glRasterPos3sv\0"
"glRasterPos4d\0"
"glRasterPos4dv\0"
"glRasterPos4f\0"
"glRasterPos4fv\0"
"glRasterPos4i\0"
"glRasterPos4iv\0"
"glRasterPos4s\0"
"glRasterPos4sv\0"
"glReadBuffer\0"
"glReadBufferRegion\0"
"glReadBufferRegionEXT\0"
"glReadInstrumentsSGIX\0"
"glReadPixels\0"
"glReadVideoPixelsSUN\0"
"glRectd\0"
"glRectdv\0"
"glRectf\0"
"glRectfv\0"
"glRecti\0"
"glRectiv\0"
"glRects\0"
"glRectsv\0"
"glReferencePlaneSGIX\0"
"glRenderbufferStorageEXT\0"
"glRenderbufferStorageMultisampleCoverageNV\0"
"glRenderbufferStorageMultisampleEXT\0"
"glRenderMode\0"
"glReplacementCodePointerSUN\0"
"glReplacementCodeubSUN\0"
"glReplacementCodeubvSUN\0"
"glReplacementCodeuiColor3fVertex3fSUN\0"
"glReplacementCodeuiColor3fVertex3fvSUN\0"
"glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
"glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
"glReplacementCodeuiColor4ubVertex3fSUN\0"
"glReplacementCodeuiColor4ubVertex3fvSUN\0"
"glReplacementCodeuiNormal3fVertex3fSUN\0"
"glReplacementCodeuiNormal3fVertex3fvSUN\0"
"glReplacementCodeuiSUN\0"
"glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
"glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
"glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
"glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
"glReplacementCodeuiTexCoord2fVertex3fSUN\0"
"glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
"glReplacementCodeuiVertex3fSUN\0"
"glReplacementCodeuiVertex3fvSUN\0"
"glReplacementCodeuivSUN\0"
"glReplacementCodeusSUN\0"
"glReplacementCodeusvSUN\0"
"glRequestResidentProgramsNV\0"
"glResetHistogram\0"
"glResetHistogramEXT\0"
"glResetMinmax\0"
"glResetMinmaxEXT\0"
"glResizeBuffersMESA\0"
"glRotated\0"
"glRotatef\0"
"glSampleCoverage\0"
"glSampleCoverageARB\0"
"glSampleMapATI\0"
"glSampleMaskEXT\0"
"glSampleMaskSGIS\0"
"glSamplePassARB\0"
"glSamplePatternEXT\0"
"glSamplePatternSGIS\0"
"glScaled\0"
"glScalef\0"
"glScissor\0"
"glSecondaryColor3b\0"
"glSecondaryColor3bEXT\0"
"glSecondaryColor3bv\0"
"glSecondaryColor3bvEXT\0"
"glSecondaryColor3d\0"
"glSecondaryColor3dEXT\0"
"glSecondaryColor3dv\0"
"glSecondaryColor3dvEXT\0"
"glSecondaryColor3f\0"
"glSecondaryColor3fEXT\0"
"glSecondaryColor3fv\0"
"glSecondaryColor3fvEXT\0"
"glSecondaryColor3hNV\0"
"glSecondaryColor3hvNV\0"
"glSecondaryColor3i\0"
"glSecondaryColor3iEXT\0"
"glSecondaryColor3iv\0"
"glSecondaryColor3ivEXT\0"
"glSecondaryColor3s\0"
"glSecondaryColor3sEXT\0"
"glSecondaryColor3sv\0"
"glSecondaryColor3svEXT\0"
"glSecondaryColor3ub\0"
"glSecondaryColor3ubEXT\0"
"glSecondaryColor3ubv\0"
"glSecondaryColor3ubvEXT\0"
"glSecondaryColor3ui\0"
"glSecondaryColor3uiEXT\0"
"glSecondaryColor3uiv\0"
"glSecondaryColor3uivEXT\0"
"glSecondaryColor3us\0"
"glSecondaryColor3usEXT\0"
"glSecondaryColor3usv\0"
"glSecondaryColor3usvEXT\0"
"glSecondaryColorPointer\0"
"glSecondaryColorPointerEXT\0"
"glSecondaryColorPointerListIBM\0"
"glSelectBuffer\0"
"glSelectTextureCoordSetSGIS\0"
"glSelectTextureSGIS\0"
"glSelectTextureTransformSGIS\0"
"glSeparableFilter2D\0"
"glSeparableFilter2DEXT\0"
"glSetFenceAPPLE\0"
"glSetFenceNV\0"
"glSetFragmentShaderConstantATI\0"
"glSetInvariantEXT\0"
"glSetLocalConstantEXT\0"
"glShadeModel\0"
"glShaderOp1EXT\0"
"glShaderOp2EXT\0"
"glShaderOp3EXT\0"
"glShaderSource\0"
"glShaderSourceARB\0"
"glSharpenTexFuncSGIS\0"
"glSpriteParameterfSGIX\0"
"glSpriteParameterfvSGIX\0"
"glSpriteParameteriSGIX\0"
"glSpriteParameterivSGIX\0"
"glStartInstrumentsSGIX\0"
"glStencilClearTagEXT\0"
"glStencilFunc\0"
"glStencilFuncSeparate\0"
"glStencilFuncSeparateATI\0"
"glStencilMask\0"
"glStencilMaskSeparate\0"
"glStencilOp\0"
"glStencilOpSeparate\0"
"glStencilOpSeparateATI\0"
"glStopInstrumentsSGIX\0"
"glStringMarkerGREMEDY\0"
"glSwapAPPLE\0"
"glSwizzleEXT\0"
"glTagSampleBufferSGIX\0"
"glTangent3bEXT\0"
"glTangent3bvEXT\0"
"glTangent3dEXT\0"
"glTangent3dvEXT\0"
"glTangent3fEXT\0"
"glTangent3fvEXT\0"
"glTangent3iEXT\0"
"glTangent3ivEXT\0"
"glTangent3sEXT\0"
"glTangent3svEXT\0"
"glTangentPointerEXT\0"
"glTbufferMask3DFX\0"
"glTestFenceAPPLE\0"
"glTestFenceNV\0"
"glTestObjectAPPLE\0"
"glTexBufferEXT\0"
"glTexBumpParameterfvATI\0"
"glTexBumpParameterivATI\0"
"glTexCoord1d\0"
"glTexCoord1dv\0"
"glTexCoord1f\0"
"glTexCoord1fv\0"
"glTexCoord1hNV\0"
"glTexCoord1hvNV\0"
"glTexCoord1i\0"
"glTexCoord1iv\0"
"glTexCoord1s\0"
"glTexCoord1sv\0"
"glTexCoord2d\0"
"glTexCoord2dv\0"
"glTexCoord2f\0"
"glTexCoord2fColor3fVertex3fSUN\0"
"glTexCoord2fColor3fVertex3fvSUN\0"
"glTexCoord2fColor4fNormal3fVertex3fSUN\0"
"glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
"glTexCoord2fColor4ubVertex3fSUN\0"
"glTexCoord2fColor4ubVertex3fvSUN\0"
"glTexCoord2fNormal3fVertex3fSUN\0"
"glTexCoord2fNormal3fVertex3fvSUN\0"
"glTexCoord2fv\0"
"glTexCoord2fVertex3fSUN\0"
"glTexCoord2fVertex3fvSUN\0"
"glTexCoord2hNV\0"
"glTexCoord2hvNV\0"
"glTexCoord2i\0"
"glTexCoord2iv\0"
"glTexCoord2s\0"
"glTexCoord2sv\0"
"glTexCoord3d\0"
"glTexCoord3dv\0"
"glTexCoord3f\0"
"glTexCoord3fv\0"
"glTexCoord3hNV\0"
"glTexCoord3hvNV\0"
"glTexCoord3i\0"
"glTexCoord3iv\0"
"glTexCoord3s\0"
"glTexCoord3sv\0"
"glTexCoord4d\0"
"glTexCoord4dv\0"
"glTexCoord4f\0"
"glTexCoord4fColor4fNormal3fVertex4fSUN\0"
"glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
"glTexCoord4fv\0"
"glTexCoord4fVertex4fSUN\0"
"glTexCoord4fVertex4fvSUN\0"
"glTexCoord4hNV\0"
"glTexCoord4hvNV\0"
"glTexCoord4i\0"
"glTexCoord4iv\0"
"glTexCoord4s\0"
"glTexCoord4sv\0"
"glTexCoordPointer\0"
"glTexCoordPointerEXT\0"
"glTexCoordPointerListIBM\0"
"glTexCoordPointervINTEL\0"
"glTexEnvf\0"
"glTexEnvfv\0"
"glTexEnvi\0"
"glTexEnviv\0"
"glTexFilterFuncSGIS\0"
"glTexGend\0"
"glTexGendv\0"
"glTexGenf\0"
"glTexGenfv\0"
"glTexGeni\0"
"glTexGeniv\0"
"glTexImage1D\0"
"glTexImage2D\0"
"glTexImage3D\0"
"glTexImage3DEXT\0"
"glTexImage4DSGIS\0"
"glTexParameterf\0"
"glTexParameterfv\0"
"glTexParameteri\0"
"glTexParameterIivEXT\0"
"glTexParameterIuivEXT\0"
"glTexParameteriv\0"
"glTexScissorFuncINTEL\0"
"glTexScissorINTEL\0"
"glTexSubImage1D\0"
"glTexSubImage1DEXT\0"
"glTexSubImage2D\0"
"glTexSubImage2DEXT\0"
"glTexSubImage3D\0"
"glTexSubImage3DEXT\0"
"glTexSubImage4DSGIS\0"
"glTextureColorMaskSGIS\0"
"glTextureFogSGIX\0"
"glTextureLightEXT\0"
"glTextureMaterialEXT\0"
"glTextureNormalEXT\0"
"glTextureRangeAPPLE\0"
"glTrackMatrixNV\0"
"glTransformFeedbackAttribsNV\0"
"glTransformFeedbackVaryingsNV\0"
"glTranslated\0"
"glTranslatef\0"
"glUniform1f\0"
"glUniform1fARB\0"
"glUniform1fv\0"
"glUniform1fvARB\0"
"glUniform1i\0"
"glUniform1iARB\0"
"glUniform1iv\0"
"glUniform1ivARB\0"
"glUniform1uiEXT\0"
"glUniform1uivEXT\0"
"glUniform2f\0"
"glUniform2fARB\0"
"glUniform2fv\0"
"glUniform2fvARB\0"
"glUniform2i\0"
"glUniform2iARB\0"
"glUniform2iv\0"
"glUniform2ivARB\0"
"glUniform2uiEXT\0"
"glUniform2uivEXT\0"
"glUniform3f\0"
"glUniform3fARB\0"
"glUniform3fv\0"
"glUniform3fvARB\0"
"glUniform3i\0"
"glUniform3iARB\0"
"glUniform3iv\0"
"glUniform3ivARB\0"
"glUniform3uiEXT\0"
"glUniform3uivEXT\0"
"glUniform4f\0"
"glUniform4fARB\0"
"glUniform4fv\0"
"glUniform4fvARB\0"
"glUniform4i\0"
"glUniform4iARB\0"
"glUniform4iv\0"
"glUniform4ivARB\0"
"glUniform4uiEXT\0"
"glUniform4uivEXT\0"
"glUniformBufferEXT\0"
"glUniformMatrix2fv\0"
"glUniformMatrix2fvARB\0"
"glUniformMatrix2x3fv\0"
"glUniformMatrix2x4fv\0"
"glUniformMatrix3fv\0"
"glUniformMatrix3fvARB\0"
"glUniformMatrix3x2fv\0"
"glUniformMatrix3x4fv\0"
"glUniformMatrix4fv\0"
"glUniformMatrix4fvARB\0"
"glUniformMatrix4x2fv\0"
"glUniformMatrix4x3fv\0"
"glUnlockArraysEXT\0"
"glUnmapBuffer\0"
"glUnmapBufferARB\0"
"glUnmapObjectBufferATI\0"
"glUnmapTexture3DATI\0"
"glUpdateObjectBufferATI\0"
"glUseProgram\0"
"glUseProgramObjectARB\0"
"glValidateProgram\0"
"glValidateProgramARB\0"
"glValidBackBufferHintAutodesk\0"
"glVariantArrayObjectATI\0"
"glVariantbvEXT\0"
"glVariantdvEXT\0"
"glVariantfvEXT\0"
"glVariantivEXT\0"
"glVariantPointerEXT\0"
"glVariantsvEXT\0"
"glVariantubvEXT\0"
"glVariantuivEXT\0"
"glVariantusvEXT\0"
"glVertex2d\0"
"glVertex2dv\0"
"glVertex2f\0"
"glVertex2fv\0"
"glVertex2hNV\0"
"glVertex2hvNV\0"
"glVertex2i\0"
"glVertex2iv\0"
"glVertex2s\0"
"glVertex2sv\0"
"glVertex3d\0"
"glVertex3dv\0"
"glVertex3f\0"
"glVertex3fv\0"
"glVertex3hNV\0"
"glVertex3hvNV\0"
"glVertex3i\0"
"glVertex3iv\0"
"glVertex3s\0"
"glVertex3sv\0"
"glVertex4d\0"
"glVertex4dv\0"
"glVertex4f\0"
"glVertex4fv\0"
"glVertex4hNV\0"
"glVertex4hvNV\0"
"glVertex4i\0"
"glVertex4iv\0"
"glVertex4s\0"
"glVertex4sv\0"
"glVertexArrayParameteriAPPLE\0"
"glVertexArrayRangeAPPLE\0"
"glVertexArrayRangeNV\0"
"glVertexAttrib1d\0"
"glVertexAttrib1dARB\0"
"glVertexAttrib1dNV\0"
"glVertexAttrib1dv\0"
"glVertexAttrib1dvARB\0"
"glVertexAttrib1dvNV\0"
"glVertexAttrib1f\0"
"glVertexAttrib1fARB\0"
"glVertexAttrib1fNV\0"
"glVertexAttrib1fv\0"
"glVertexAttrib1fvARB\0"
"glVertexAttrib1fvNV\0"
"glVertexAttrib1hNV\0"
"glVertexAttrib1hvNV\0"
"glVertexAttrib1s\0"
"glVertexAttrib1sARB\0"
"glVertexAttrib1sNV\0"
"glVertexAttrib1sv\0"
"glVertexAttrib1svARB\0"
"glVertexAttrib1svNV\0"
"glVertexAttrib2d\0"
"glVertexAttrib2dARB\0"
"glVertexAttrib2dNV\0"
"glVertexAttrib2dv\0"
"glVertexAttrib2dvARB\0"
"glVertexAttrib2dvNV\0"
"glVertexAttrib2f\0"
"glVertexAttrib2fARB\0"
"glVertexAttrib2fNV\0"
"glVertexAttrib2fv\0"
"glVertexAttrib2fvARB\0"
"glVertexAttrib2fvNV\0"
"glVertexAttrib2hNV\0"
"glVertexAttrib2hvNV\0"
"glVertexAttrib2s\0"
"glVertexAttrib2sARB\0"
"glVertexAttrib2sNV\0"
"glVertexAttrib2sv\0"
"glVertexAttrib2svARB\0"
"glVertexAttrib2svNV\0"
"glVertexAttrib3d\0"
"glVertexAttrib3dARB\0"
"glVertexAttrib3dNV\0"
"glVertexAttrib3dv\0"
"glVertexAttrib3dvARB\0"
"glVertexAttrib3dvNV\0"
"glVertexAttrib3f\0"
"glVertexAttrib3fARB\0"
"glVertexAttrib3fNV\0"
"glVertexAttrib3fv\0"
"glVertexAttrib3fvARB\0"
"glVertexAttrib3fvNV\0"
"glVertexAttrib3hNV\0"
"glVertexAttrib3hvNV\0"
"glVertexAttrib3s\0"
"glVertexAttrib3sARB\0"
"glVertexAttrib3sNV\0"
"glVertexAttrib3sv\0"
"glVertexAttrib3svARB\0"
"glVertexAttrib3svNV\0"
"glVertexAttrib4bv\0"
"glVertexAttrib4bvARB\0"
"glVertexAttrib4d\0"
"glVertexAttrib4dARB\0"
"glVertexAttrib4dNV\0"
"glVertexAttrib4dv\0"
"glVertexAttrib4dvARB\0"
"glVertexAttrib4dvNV\0"
"glVertexAttrib4f\0"
"glVertexAttrib4fARB\0"
"glVertexAttrib4fNV\0"
"glVertexAttrib4fv\0"
"glVertexAttrib4fvARB\0"
"glVertexAttrib4fvNV\0"
"glVertexAttrib4hNV\0"
"glVertexAttrib4hvNV\0"
"glVertexAttrib4iv\0"
"glVertexAttrib4ivARB\0"
"glVertexAttrib4Nbv\0"
"glVertexAttrib4NbvARB\0"
"glVertexAttrib4Niv\0"
"glVertexAttrib4NivARB\0"
"glVertexAttrib4Nsv\0"
"glVertexAttrib4NsvARB\0"
"glVertexAttrib4Nub\0"
"glVertexAttrib4NubARB\0"
"glVertexAttrib4Nubv\0"
"glVertexAttrib4NubvARB\0"
"glVertexAttrib4Nuiv\0"
"glVertexAttrib4NuivARB\0"
"glVertexAttrib4Nusv\0"
"glVertexAttrib4NusvARB\0"
"glVertexAttrib4s\0"
"glVertexAttrib4sARB\0"
"glVertexAttrib4sNV\0"
"glVertexAttrib4sv\0"
"glVertexAttrib4svARB\0"
"glVertexAttrib4svNV\0"
"glVertexAttrib4ubNV\0"
"glVertexAttrib4ubv\0"
"glVertexAttrib4ubvARB\0"
"glVertexAttrib4ubvNV\0"
"glVertexAttrib4uiv\0"
"glVertexAttrib4uivARB\0"
"glVertexAttrib4usv\0"
"glVertexAttrib4usvARB\0"
"glVertexAttribArrayObjectATI\0"
"glVertexAttribI1iEXT\0"
"glVertexAttribI1ivEXT\0"
"glVertexAttribI1uiEXT\0"
"glVertexAttribI1uivEXT\0"
"glVertexAttribI2iEXT\0"
"glVertexAttribI2ivEXT\0"
"glVertexAttribI2uiEXT\0"
"glVertexAttribI2uivEXT\0"
"glVertexAttribI3iEXT\0"
"glVertexAttribI3ivEXT\0"
"glVertexAttribI3uiEXT\0"
"glVertexAttribI3uivEXT\0"
"glVertexAttribI4bvEXT\0"
"glVertexAttribI4iEXT\0"
"glVertexAttribI4ivEXT\0"
"glVertexAttribI4svEXT\0"
"glVertexAttribI4ubvEXT\0"
"glVertexAttribI4uiEXT\0"
"glVertexAttribI4uivEXT\0"
"glVertexAttribI4usvEXT\0"
"glVertexAttribIPointerEXT\0"
"glVertexAttribPointer\0"
"glVertexAttribPointerARB\0"
"glVertexAttribPointerNV\0"
"glVertexAttribs1dvNV\0"
"glVertexAttribs1fvNV\0"
"glVertexAttribs1hvNV\0"
"glVertexAttribs1svNV\0"
"glVertexAttribs2dvNV\0"
"glVertexAttribs2fvNV\0"
"glVertexAttribs2hvNV\0"
"glVertexAttribs2svNV\0"
"glVertexAttribs3dvNV\0"
"glVertexAttribs3fvNV\0"
"glVertexAttribs3hvNV\0"
"glVertexAttribs3svNV\0"
"glVertexAttribs4dvNV\0"
"glVertexAttribs4fvNV\0"
"glVertexAttribs4hvNV\0"
"glVertexAttribs4svNV\0"
"glVertexAttribs4ubvNV\0"
"glVertexBlendARB\0"
"glVertexBlendEnvfATI\0"
"glVertexBlendEnviATI\0"
"glVertexPointer\0"
"glVertexPointerEXT\0"
"glVertexPointerListIBM\0"
"glVertexPointervINTEL\0"
"glVertexStream1dATI\0"
"glVertexStream1dvATI\0"
"glVertexStream1fATI\0"
"glVertexStream1fvATI\0"
"glVertexStream1iATI\0"
"glVertexStream1ivATI\0"
"glVertexStream1sATI\0"
"glVertexStream1svATI\0"
"glVertexStream2dATI\0"
"glVertexStream2dvATI\0"
"glVertexStream2fATI\0"
"glVertexStream2fvATI\0"
"glVertexStream2iATI\0"
"glVertexStream2ivATI\0"
"glVertexStream2sATI\0"
"glVertexStream2svATI\0"
"glVertexStream3dATI\0"
"glVertexStream3dvATI\0"
"glVertexStream3fATI\0"
"glVertexStream3fvATI\0"
"glVertexStream3iATI\0"
"glVertexStream3ivATI\0"
"glVertexStream3sATI\0"
"glVertexStream3svATI\0"
"glVertexStream4dATI\0"
"glVertexStream4dvATI\0"
"glVertexStream4fATI\0"
"glVertexStream4fvATI\0"
"glVertexStream4iATI\0"
"glVertexStream4ivATI\0"
"glVertexStream4sATI\0"
"glVertexStream4svATI\0"
"glVertexWeightfEXT\0"
"glVertexWeightfvEXT\0"
"glVertexWeighthNV\0"
"glVertexWeighthvNV\0"
"glVertexWeightPointerEXT\0"
"glViewport\0"
"glWeightbvARB\0"
"glWeightdvARB\0"
"glWeightfvARB\0"
"glWeightivARB\0"
"glWeightPointerARB\0"
"glWeightsvARB\0"
"glWeightubvARB\0"
"glWeightuivARB\0"
"glWeightusvARB\0"
"glWindowBackBufferHintAutodesk\0"
"glWindowPos2d\0"
"glWindowPos2dARB\0"
"glWindowPos2dMESA\0"
"glWindowPos2dv\0"
"glWindowPos2dvARB\0"
"glWindowPos2dvMESA\0"
"glWindowPos2f\0"
"glWindowPos2fARB\0"
"glWindowPos2fMESA\0"
"glWindowPos2fv\0"
"glWindowPos2fvARB\0"
"glWindowPos2fvMESA\0"
"glWindowPos2i\0"
"glWindowPos2iARB\0"
"glWindowPos2iMESA\0"
"glWindowPos2iv\0"
"glWindowPos2ivARB\0"
"glWindowPos2ivMESA\0"
"glWindowPos2s\0"
"glWindowPos2sARB\0"
"glWindowPos2sMESA\0"
"glWindowPos2sv\0"
"glWindowPos2svARB\0"
"glWindowPos2svMESA\0"
"glWindowPos3d\0"
"glWindowPos3dARB\0"
"glWindowPos3dMESA\0"
"glWindowPos3dv\0"
"glWindowPos3dvARB\0"
"glWindowPos3dvMESA\0"
"glWindowPos3f\0"
"glWindowPos3fARB\0"
"glWindowPos3fMESA\0"
"glWindowPos3fv\0"
"glWindowPos3fvARB\0"
"glWindowPos3fvMESA\0"
"glWindowPos3i\0"
"glWindowPos3iARB\0"
"glWindowPos3iMESA\0"
"glWindowPos3iv\0"
"glWindowPos3ivARB\0"
"glWindowPos3ivMESA\0"
"glWindowPos3s\0"
"glWindowPos3sARB\0"
"glWindowPos3sMESA\0"
"glWindowPos3sv\0"
"glWindowPos3svARB\0"
"glWindowPos3svMESA\0"
"glWindowPos4dMESA\0"
"glWindowPos4dvMESA\0"
"glWindowPos4fMESA\0"
"glWindowPos4fvMESA\0"
"glWindowPos4iMESA\0"
"glWindowPos4ivMESA\0"
"glWindowPos4sMESA\0"
"glWindowPos4svMESA\0"
"glWriteMaskEXT\0"
"\0"
};

typedef struct {
    unsigned int hash;
    char *name;
    __GLXextFuncPtr func;
    unsigned char flag;
} AssocProcAdress;

#define FOUND_ON_SERVER(i) (tab_assoc[i].flag & 0x01)
#define FOUND_ON_CLIENT(i) (tab_assoc[i].flag & 0x02)
#define QUERIED_ONCE(i)    (tab_assoc[i].flag & 0x04)

#define SET_FOUND_ON_SERVER(i) tab_assoc[i].flag |= 0x01
#define SET_FOUND_ON_CLIENT(i) tab_assoc[i].flag |= 0x02
#define SET_QUERIED_ONCE(i)    tab_assoc[i].flag |= 0x04

#define UNSET_FOUND_ON_SERVER(i) tab_assoc[i].flag &= ~0x01
#define UNSET_FOUND_ON_CLIENT(i) tab_assoc[i].flag &= ~0x02
#define UNSET_QUERIED_ONCE(i)    tab_assoc[i].flag &= ~0x04

static __GLXextFuncPtr glXGetProcAddress_no_lock(const char *_name)
{
    if (_name == NULL) {
        return NULL;
    }

    static int nbElts = 0;
    static int tabSize = 0;
    static AssocProcAdress *tab_assoc = NULL;
    static void *handle = NULL;

    int i;
    
    if (tabSize == 0) {
        tabSize = 2000;
        tab_assoc = calloc(tabSize, sizeof(AssocProcAdress));
        
        if (!(handle = dlopen("libGL.so", RTLD_LAZY))) {
            log_gl("%s\n", dlerror());
            exit(1);
        }
        
        int sizeOfString = 0;
        int nbRequestElts = 0;
        int i = 0;
        while (1) {
            if (global_glXGetProcAddress_request[i] == '\0') {
                nbRequestElts++;
                if (global_glXGetProcAddress_request[i + 1] == '\0') {
                    sizeOfString = i + 1;
                    break;
                }
            }
            i++;
        }
        char *result = (char *)malloc(nbRequestElts);
        
        long args[] = {INT_TO_ARG(nbRequestElts),
                       POINTER_TO_ARG(global_glXGetProcAddress_request),
                       POINTER_TO_ARG(result)};
        int args_size[] = {0, sizeOfString, nbRequestElts};
        do_opengl_call_no_lock(glXGetProcAddress_global_fake_func, NULL,
                               CHECK_ARGS(args, args_size));
        int offset = 0;
        for (i = 0; i < nbRequestElts; i++) {
            const char *funcName = global_glXGetProcAddress_request + offset;
            void *func = dlsym(handle, funcName);
            if (nbElts < tabSize) {
                int hash = str_hash(funcName);
                tab_assoc[nbElts].hash = hash;
                tab_assoc[nbElts].name = strdup(funcName);
                UNSET_QUERIED_ONCE(nbElts);
                if (result[i]) {
                    SET_FOUND_ON_SERVER(nbElts);
                    tab_assoc[nbElts].func = func;
                } else {
                    UNSET_FOUND_ON_SERVER(nbElts);
                    tab_assoc[nbElts].func = NULL;
                }
                if (func) {
                    SET_FOUND_ON_CLIENT(nbElts);
                } else {
                    UNSET_FOUND_ON_CLIENT(nbElts);
                }
            }
            offset += strlen(funcName) + 1;
        }
        free(result);
        
        return glXGetProcAddress_no_lock(_name);
    }
    
    const char *name = (const char *)_name;
    DEBUGLOG_GL("looking for \"%s\",\n", name);
    int hash = str_hash(name);
    for (i = 0; i < nbElts; i++) {
        if (tab_assoc[i].hash == hash && !strcmp(tab_assoc[i].name, name)) {
            if (!QUERIED_ONCE(i)) {
                SET_QUERIED_ONCE(i);
                if (!FOUND_ON_SERVER(i)) {
                    if (!FOUND_ON_CLIENT(i)) {
                        log_gl("not found on server nor client: %s\n", name);
                    } else {
                        log_gl("not found on server: %s\n", name);
                    }
                } else if (!FOUND_ON_CLIENT(i)) {
                    log_gl("not on client: %s\n", name);
                }
            }
            return tab_assoc[i].func;
        }
    }
    
    DEBUGLOG_GL("looking for non-precached \"%s\",\n", name);
    int ret_call = 0;
    void *func = dlsym(handle, name);
    if (name[0] == 'g' && name[1] == 'l' && name[2] == 'X') {
        if (func) {
            ret_call = 1;
        }
    } else {
        long args[] = {INT_TO_ARG(name)};
        do_opengl_call_no_lock(glXGetProcAddress_fake_func, &ret_call, args, NULL);
    }
    __GLXextFuncPtr ret = NULL;
    if (nbElts < tabSize) {
        tab_assoc[nbElts].hash = hash;
        tab_assoc[nbElts].name = strdup(name);
        SET_QUERIED_ONCE(nbElts);
        if (ret_call) {
            SET_FOUND_ON_SERVER(nbElts);
            tab_assoc[nbElts].func = func;
            ret = func;
        } else {
            UNSET_FOUND_ON_SERVER(nbElts);
            tab_assoc[nbElts].func = NULL;
        }
        if (func) {
            SET_FOUND_ON_CLIENT(nbElts);
        } else {
            UNSET_FOUND_ON_CLIENT(nbElts);
        }
        nbElts++;
    }
    if (!ret_call) {
        if (!func) {
            log_gl("not found on server nor client: %s\n", name);
        } else {
            log_gl("not found on server: %s\n", name);
        }
    } else if (!func) {
        log_gl("not found on client: %s\n", name);
    }
    return ret;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    LOCK(glXGetProcAddress_fake_func);
    __GLXextFuncPtr ret = glXGetProcAddress_no_lock((const char *)name);
    UNLOCK(glXGetProcAddress_fake_func);
    return ret;
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name)
{
    return glXGetProcAddress(name);
}
