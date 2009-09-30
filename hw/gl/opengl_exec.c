/*
 * Host-side implementation of GL/GLX API
 * 
 * Copyright (c) 2006,2007 Even Rouault
 * Copyright (c) 2009 Nokia Corporation
 *
 * NOTE: in its current state this code works only for 32bit guests AND
 * when guest endianess equals host endianess.
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

#include "qemu-common.h"
#include "opengl_exec.h"

#define GL_GLEXT_PROTOTYPES
#define GLX_GLXEXT_PROTOTYPES

#include "mesa_gl.h"

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", \
                                  __FUNCTION__, __LINE__, ##__VA_ARGS__)
#ifdef USE_OSMESA
#include "opengl_host_osmesa.h"
#else
#ifdef CONFIG_COCOA
#include "opengl_host_cocoa.h"
#elif defined(WIN32)
#include "opengl_host_win32.h"
#else
#include "opengl_host_x11.h"
#endif
#endif

#include "opengl_func.h"

//#define GL_EXCESS_DEBUG

#ifdef GL_EXCESS_DEBUG
#define TRACE(fmt,...) GL_ERROR(fmt, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

//#define SYSTEMATIC_ERROR_CHECK

#ifdef USE_OSMESA
#define GET_FPTR(check, type, funcname, args_decl) \
    static int detect_##funcname = 0; \
    static type(*ptr_func_##funcname)args_decl = NULL; \
    if (detect_##funcname == 0) { \
        detect_##funcname = 1; \
        ptr_func_##funcname = (type(*)args_decl)OSMesaGetProcAddress(#funcname); \
        if (check && !ptr_func_##funcname) { \
            GL_ERROR("unable to locate \"" #funcname "\""); \
            exit(-1); \
        } \
    }
#else
#ifndef WIN32
#include <dlfcn.h>
#define GET_FPTR(check, type, funcname, args_decl) \
    static int detect_##funcname = 0; \
    static type(*ptr_func_##funcname)args_decl = NULL; \
    if (detect_##funcname == 0) { \
        detect_##funcname = 1; \
        ptr_func_##funcname = (type(*)args_decl)dlsym(RTLD_DEFAULT, \
                                                      #funcname); \
        if (check && !ptr_func_##funcname) { \
            GL_ERROR("unable to locate \"" #funcname "\": %s", dlerror()); \
            exit(-1); \
        } \
    }
#else
#define GET_FPTR(check, type, funcname, args_decl) \
    static int detect_##funcname = 0; \
    static type(*ptr_func_##funcname)args_decl = NULL; \
    if (detect_##funcname == 0) { \
        detect_##funcname = 1; \
        ptr_func_##funcname = (void *)wglGetProcAddress(#funcname); \
        int i_; \
        for (i_ = 0; !ptr_func_##funcname && search_libs_w32_[i_] != (LPCTSTR)-1; i_++) { \
            HMODULE hmod = GetModuleHandle(search_libs_w32_[i_]); \
            if (!hmod && search_libs_w32_[i_]) { \
                hmod = LoadLibrary(search_libs_w32_[i_]); \
                /* never to be released again... */ \
            } \
            if (hmod) { \
                ptr_func_##funcname = (type(*)args_decl)GetProcAddress( \
                    hmod, #funcname); \
            } \
        } \
        if (check && !ptr_func_##funcname) { \
            GL_ERROR("unable to locate \"" #funcname "\": error %d", \
                  GetLastError()); \
            exit(-1); \
        } \
    }
#endif
#endif

#define GET_EXT_PTR(...) GET_FPTR(1, __VA_ARGS__)
#define GET_EXT_PTR_NO_FAIL(...) GET_FPTR(0, __VA_ARGS__)

#define MAX_HANDLED_PROCESS 100
#define MAX_ASSOC_SIZE 100
//#define MAX_CLIENT_STATE_STACK_SIZE 16
#define ARG_TO_CHAR(x)                (char)(x)
#define ARG_TO_UNSIGNED_CHAR(x)       (unsigned char)(x)
#define ARG_TO_SHORT(x)               (short)(x)
#define ARG_TO_UNSIGNED_SHORT(x)      (unsigned short)(x)
#define ARG_TO_INT(x)                 (int)(x)
#define ARG_TO_UNSIGNED_INT(x)        (unsigned int)(x)
#define ARG_TO_FLOAT(x)               (*(float*)&(x))
#define ARG_TO_DOUBLE(x)              (*(double*)(x))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

typedef struct {
    int key;
    void *value;
} IntAssoc;

typedef struct {
    int ref;
    int client_ctx;
    int client_share;
    GLHostContext context;
    GLHostDrawable drawable;
#ifdef SYSTEMATIC_ERROR_CHECK
    int last_error;
#endif
} GLState;

typedef struct {
    int internal_num;
    int process_id;
    int instr_counter;

    int width, height, depth;
    uint32_t shmaddr;

    int next_available_context_number;
    int next_available_pbuffer_number;

    int nbGLStates;
    GLState default_state;
    GLState **glstates;
    GLState *current_state;

    IntAssoc assoc_Cctx_Sctx[MAX_ASSOC_SIZE + 1];
    IntAssoc assoc_Cpbuf_Spbuf[MAX_ASSOC_SIZE + 1];
    IntAssoc assoc_Cdrw_Sdrw[MAX_ASSOC_SIZE + 1];
    IntAssoc assoc_Cctx_Svis[MAX_ASSOC_SIZE + 1];
} ProcessStruct;

static ProcessStruct processTab[MAX_HANDLED_PROCESS];
#ifndef QEMUGL_MULTITHREADED
int last_process_id = 0;
#endif

void init_process_tab(void)
{
    memset(processTab, 0, sizeof(processTab));
}

typedef struct {
    int attribListLength;
    int *attribList;
    GLHostVisualInfo visInfo;
} AssocAttribListVisual;

static int nTabAssocAttribListVisual = 0;
static AssocAttribListVisual *tabAssocAttribListVisual = NULL;

static int compute_length_of_attrib_list_including_zero(const int *attribList,
                                                        int boolMustHaveValue)
{
    int i = 0;
    while (attribList[i]) {
        if (boolMustHaveValue ||
            !(attribList[i] == GLX_USE_GL ||
              attribList[i] == GLX_RGBA ||
              attribList[i] == GLX_DOUBLEBUFFER ||
              attribList[i] == GLX_STEREO)) {
            i += 2;
        } else {
            i++;
        }
    }
    return i + 1;
}

#include "server_stub.c"

/* ---- */

static void *assoc_get_(const IntAssoc *list, int key)
{
    int i;
    for (i = 0; i < MAX_ASSOC_SIZE && list[i].key; i++) {
        if (list[i].key == key) {
            return list[i].value;
        }
    }
    return NULL;
}

static void assoc_set_(IntAssoc *list, int key, const void *value)
{
    int i;
    for (i = 0; i < MAX_ASSOC_SIZE && list[i].key; i++) {
        if (list[i].key == key) {
            break;
        }
    }
    if (i < MAX_ASSOC_SIZE) {
        list[i].key = key;
        list[i].value = (void *)value;
    } else {
        GL_ERROR("MAX_ASSOC_SIZE reached");
    }
}

static void assoc_unset_(IntAssoc *list, int key)
{
    int i;
    for (i = 0; i < MAX_ASSOC_SIZE && list[i].key; i++) {
        if (list[i].key == key) {
            memmove(&list[i],
                    &list[i + 1],
                    sizeof(IntAssoc) * (MAX_ASSOC_SIZE - 1 - i));
            return;
        }
    }
}

/* ---- */

static inline GLHostContext assoc_get_Cctx_Sctx(ProcessStruct *process,
                                                int cctx)
{
    return (GLHostContext)assoc_get_(process->assoc_Cctx_Sctx, cctx);
}

static inline GLHostVisualInfo assoc_get_Cctx_Svis(ProcessStruct *process,
                                                    int cctx)
{
    return (GLHostVisualInfo)assoc_get_(process->assoc_Cctx_Svis, cctx);
}

static inline GLHostPbuffer assoc_get_Cpbuf_Spbuf(ProcessStruct *process,
                                                  int cpbuf)
{
    return (GLHostPbuffer)assoc_get_(process->assoc_Cpbuf_Spbuf, cpbuf);
}

static inline GLHostDrawable assoc_get_Cdrw_Sdrw(ProcessStruct *process,
                                                 int cdrw)
{
    return (GLHostDrawable)assoc_get_(process->assoc_Cdrw_Sdrw, cdrw);
}

/* ---- */

static inline void assoc_set_Cctx_Sctx(ProcessStruct *process,
                                       int cctx, const GLHostContext sctx)
{
    assoc_set_(process->assoc_Cctx_Sctx, cctx, (const void *)sctx);
}

static inline void assoc_set_Cctx_Svis(ProcessStruct *process,
                                       int cctx, const GLHostVisualInfo svis)
{
    assoc_set_(process->assoc_Cctx_Svis, cctx, (const void *)svis);
}

static inline void assoc_set_Cpbuf_Spbuf(ProcessStruct *process,
                                         int cpbuf, const GLHostPbuffer spbuf)
{
    assoc_set_(process->assoc_Cpbuf_Spbuf, cpbuf, (const void *)spbuf);
}

static inline void assoc_set_Cdrw_Sdrw(ProcessStruct *process,
                                       int cdrw, const GLHostDrawable sdrw)
{
    assoc_set_(process->assoc_Cdrw_Sdrw, cdrw, (const void *)sdrw);
}

/* ---- */

static inline void assoc_unset_Cctx_Sctx(ProcessStruct *process, int cctx)
{
    assoc_unset_(process->assoc_Cctx_Sctx, cctx);
}

static inline void assoc_unset_Cpbuf_Spbuf(ProcessStruct *process, int cpbuf)
{
    assoc_unset_(process->assoc_Cpbuf_Spbuf, cpbuf);
}

/* ---- */

static void doExitProcess(int pid)
{
    ProcessStruct *process = &processTab[pid];
    int i;
    for (i = 0; i < MAX_ASSOC_SIZE && process->assoc_Cctx_Sctx[i].key; i++) {
        TRACE("Destroy context corresponding to client context %d",
              process->assoc_Cctx_Sctx[i].key);
        GLHostContext c = (GLHostContext)process->assoc_Cctx_Sctx[i].value;
        glhost_destroycontext(c);
    }

    for (i = 0; i < MAX_ASSOC_SIZE && process->assoc_Cpbuf_Spbuf[i].key; i++) {
        TRACE("Destroy pbuffer corresponding to client pbuffer %d",
              process->assoc_Cpbuf_Spbuf[i].key);
        GLHostPbuffer p = (GLHostPbuffer)process->assoc_Cpbuf_Spbuf[i].value;
        glhost_destroypbuffer(p);
    }
    
    for (i = 0; i < MAX_ASSOC_SIZE && process->assoc_Cdrw_Sdrw[i].key; i++) {
        TRACE("Destroy drawable corresponding to client drawable %d",
              process->assoc_Cdrw_Sdrw[i].key);
        GLHostDrawable d = (GLHostDrawable)process->assoc_Cdrw_Sdrw[i].value;
        glhost_destroydrawable(d);
    }
    
    for (i = 0; i < process->nbGLStates; i++) {
        free(process->glstates[i]);
    }
    free(process->glstates);
    
    memmove(&processTab[pid], &processTab[pid + 1],
            (MAX_HANDLED_PROCESS - 1 - pid) * sizeof(ProcessStruct));
}

static void doCreateDrawable(ProcessStruct *process, int client_drawable,
                             uint32_t shmaddr, int depth, int width,
                             int height)
{
    TRACE("client_drawable 0x%x, size %dx%d, depth %d, addr 0x%08x",
          client_drawable, width, height, depth, shmaddr);
    process->width =  width;
    process->height = height;
    process->depth =  depth;
    process->shmaddr = shmaddr;
    /* let's not create it yet, though... */
}

static void doResizeDrawable(ProcessStruct *process, int client_drawable,
                             int width, int height, uint32_t shmaddr)
{
    TRACE("client_drawable 0x%x, size %dx%d, addr 0x%08x",
          client_drawable, width, height, shmaddr);
    process->width = width;
    process->height = height;
    process->shmaddr = shmaddr;
    GLHostDrawable d = assoc_get_Cdrw_Sdrw(process, client_drawable);
    if (d) {
        glhost_resizedrawable(d, width, height);
    } else {
        GL_ERROR("unknown client drawable 0x%08x", client_drawable);
    }
}

static int doChooseVisual(int depth, const int *attribList)
{
    if (attribList == NULL) {
        return 0;
    }
    int attribListLength =
    compute_length_of_attrib_list_including_zero(attribList, 0);
    
    int i;
    for (i = 0; i < nTabAssocAttribListVisual; i++) {
        if (tabAssocAttribListVisual[i].attribListLength == attribListLength &&
            memcmp(tabAssocAttribListVisual[i].attribList, attribList,
                   attribListLength * sizeof(int)) == 0) {
            return (tabAssocAttribListVisual[i].visInfo)
            ? glhost_idforvisual(tabAssocAttribListVisual[i].visInfo) : 0;
        }
    }
    
    GLHostVisualInfo visInfo = glhost_choosevisual(depth, attribList);
    tabAssocAttribListVisual =
    realloc(tabAssocAttribListVisual,
            sizeof(AssocAttribListVisual)
            * (nTabAssocAttribListVisual + 1));
    tabAssocAttribListVisual[nTabAssocAttribListVisual].attribListLength =
    attribListLength;
    tabAssocAttribListVisual[nTabAssocAttribListVisual].attribList =
        (int *)malloc(sizeof(int) * attribListLength);
    memcpy(tabAssocAttribListVisual[nTabAssocAttribListVisual].attribList,
           attribList, sizeof(int) * attribListLength);
    tabAssocAttribListVisual[nTabAssocAttribListVisual].visInfo = visInfo;
    nTabAssocAttribListVisual++;
    return (visInfo) ? glhost_idforvisual(visInfo) : 0;
}

static void create_context(ProcessStruct *process,
                           GLHostContext sctx, int cctx,
                           GLHostContext shareList, int cShareList)
{
    process->glstates = realloc(process->glstates,
                                (process->nbGLStates + 1) * sizeof(GLState *));
    process->glstates[process->nbGLStates] = malloc(sizeof(GLState));
    memset(process->glstates[process->nbGLStates], 0, sizeof(GLState));
    process->glstates[process->nbGLStates]->ref = 1;
    process->glstates[process->nbGLStates]->context = sctx;
    process->glstates[process->nbGLStates]->client_ctx = cctx;
    process->glstates[process->nbGLStates]->client_share = cShareList;
    if (shareList && cShareList) {
        int i;
        for (i = 0; i < process->nbGLStates; i++) {
            if (process->glstates[i]->client_ctx == cShareList) {
                process->glstates[i]->ref++;
                break;
            }
        }
    }
    process->nbGLStates++;
}

static int doCreateContext(ProcessStruct *process, int visualid, int cShList)
{
    TRACE("visualid=%d, shareList=%d", visualid, cShList);
    GLHostContext shareList = assoc_get_Cctx_Sctx(process, cShList);
    GLHostVisualInfo vis = glhost_getvisualinfo(visualid);
    GLHostContext ctxt;
    if (vis) {
        ctxt = glhost_createcontext(vis, shareList);
    } else {
        vis = glhost_getdefaultvisual();
        ctxt = glhost_createcontext(vis, shareList);
    }
    
    int ret_ctx = 0;
    if (ctxt) {
        ret_ctx = ++process->next_available_context_number;
        assoc_set_Cctx_Svis(process, ret_ctx, vis);
        assoc_set_Cctx_Sctx(process, ret_ctx, ctxt);
        create_context(process, ctxt, ret_ctx, shareList, cShList);
    }
    return ret_ctx;
}

static void doCopyContext(ProcessStruct *process, int src, int dst,
                          unsigned long mask)
{
    GLHostContext src_ctxt;
    GLHostContext dst_ctxt;
    if ((src_ctxt = assoc_get_Cctx_Sctx(process, src)) == NULL) {
        GL_ERROR("invalid client src context (%d)!", src);
    } else if ((dst_ctxt = assoc_get_Cctx_Sctx(process, dst)) == NULL) {
        GL_ERROR("invalid client dst context (%d)!", dst);
    } else {
        glhost_copycontext(src_ctxt, dst_ctxt, mask);
    }
}

static void doDestroyContext(ProcessStruct *process, int cctx)
{
    TRACE("client context %d", cctx);
    GLHostContext ctxt = assoc_get_Cctx_Sctx(process, cctx);
    if (ctxt == NULL) {
        GL_ERROR("invalid client context (%d)!", cctx);
    } else {
        int i;
        for (i = 0; i < process->nbGLStates; i++) {
            if (process->glstates[i]->client_ctx == cctx) {
                if (ctxt == process->current_state->context) {
                    process->current_state = &process->default_state;
                }
                int cShareList = process->glstates[i]->client_share;
                process->glstates[i]->ref--;
                if (process->glstates[i]->ref == 0) {
                    TRACE("destroy_gl_state %p", process->glstates[i]);
                    free(process->glstates[i]);
                    memmove(&process->glstates[i], &process->glstates[i + 1],
                            (process->nbGLStates - i - 1) * sizeof(GLState *));
                    process->nbGLStates--;
                }
                if (cShareList) {
                    for (i = 0; i < process->nbGLStates; i++) {
                        if (process->glstates[i]->client_ctx == cShareList) {
                            process->glstates[i]->ref--;
                            if (process->glstates[i]->ref == 0) {
                                TRACE("destroy_gl_state %p",
                                      process->glstates[i]);
                                free(process->glstates[i]);
                                memmove(&process->glstates[i],
                                        &process->glstates[i + 1],
                                        (process->nbGLStates - i - 1) 
                                            * sizeof(GLState *));
                                process->nbGLStates--;
                            }
                            break;
                        }
                    }
                }
                glhost_destroycontext(ctxt);
                assoc_unset_Cctx_Sctx(process, cctx);
                break;
            }
        }
    }
}

static int doMakeCurrent(ProcessStruct *process, int cdrw, int cctx)
{
    TRACE("drawable=0x%x ctx=%d", cdrw, cctx);
    int i;
    int ret_int;
    GLHostDrawable drawable = 0;
    
    if (cdrw == 0 && cctx == 0) {
        ret_int = glhost_makecurrent(0, 0);
        process->current_state = &process->default_state;
    } else {
        GLHostContext ctxt = assoc_get_Cctx_Sctx(process, cctx);
        if (ctxt == NULL) {
            GL_ERROR("invalid client context (%d)!", cctx);
            ret_int = 0;
        } else {
            drawable = (GLHostDrawable)assoc_get_Cpbuf_Spbuf(process, cdrw);
            if (!drawable) {
                drawable = assoc_get_Cdrw_Sdrw(process, cdrw);
                if (!drawable) {
                    GLHostVisualInfo vis = assoc_get_Cctx_Svis(process, cctx);
                    if (!vis) {
                        vis = glhost_getdefaultvisual();
                    }
                    if (!process->width || !process->height) {
                        TRACE("size of client drawable 0x%x unknown!", cdrw);
                    } else {
                        drawable = glhost_createdrawable(vis,
                                                         process->width,
                                                         process->height,
                                                         process->depth);
                        assoc_set_Cdrw_Sdrw(process, cdrw, drawable);
                    }
                }
            }
            ret_int = drawable ? glhost_makecurrent(drawable, ctxt) : 0;
        }
    }
    
    if (ret_int) {
        for (i = 0; i < process->nbGLStates; i++) {
            if (process->glstates[i]->client_ctx == cctx) {
                process->current_state = process->glstates[i]; /* FIXME: HACK */
                process->current_state->drawable = drawable;
                break;
            }
        }
    }
    return ret_int;
}

static void doSwapBuffers(struct helper_opengl_s *s, ProcessStruct *process,
                          int cdrw)
{
    TRACE("drawable 0x%x", cdrw);
    GLHostDrawable drawable = assoc_get_Cdrw_Sdrw(process, cdrw);
    if (!drawable) {
        GL_ERROR("unknown client drawable (0x%x)!", cdrw);
    } else {
        if ((s->buf = glhost_swapbuffers(drawable))) {
            s->bufpixelsize = (process->depth + 7) / 8;
            s->bufsize = process->width * process->height;
            s->bufwidth = process->width;
#if defined(USE_OSMESA) || defined(WIN32)
            /* win32 and osmesa render scanlines in reverse order (bottom-up)
             * win32 further aligns buffer width on 32bit boundary */
            s->bufcol = 0;
#ifdef USE_OSMESA
#ifdef GLX_OSMESA_FORCE_32BPP
            s->buf += (process->width * 4) * (process->height - 1);
#else
            s->buf += (process->width * s->bufpixelsize) * (process->height - 1);
#endif // GLX_OSMESA_FORCE_32BPPP
#else
#ifdef WIN32
            s->buf += (((process->width * s->bufpixelsize) + 3) & ~3)
                      * (process->height - 1);
#endif // WIN32
#endif // USE_OSMESA
#endif // USE_OSMESA || WIN32
#ifndef QEMUGL_IO_FRAMEBUFFER
            s->qemugl_buf = process->shmaddr;
            helper_opengl_copyframe(s);
#endif // QEMUGL_IO_FRAMEBUFFER
        }
    }
}

static void doGetConfig_extended(ProcessStruct *process, int visualid, int n,
                                 int *attribs, int *values, int *results)
{
    GLHostVisualInfo vis = visualid
        ? glhost_getvisualinfo(visualid)
        : glhost_getdefaultvisual();
    int i;
    for (i = 0; i < n; i++) {
        results[i] = glhost_getconfig(vis, attribs[i], &values[i]);
    }
}

static void doDestroyPbuffer(ProcessStruct *process, int cpbuf)
{
    TRACE("pbuffer 0x%x", cpbuf);
    GLHostPbuffer p = assoc_get_Cpbuf_Spbuf(process, cpbuf);
    if (!p) {
        GL_ERROR("invalid client pbuffer (0x%x)", cpbuf);
    } else {
        glhost_destroypbuffer(p);
        assoc_unset_Cpbuf_Spbuf(process, cpbuf);
    }
}

static const char *opengl_strtok(const char *s, int *n, char **real)
{
    static char *buffer = 0;
    static int buffersize = -1;
    static const char *delim = " \t\n\r";
    static const char *prev = 0;
    if (!s) {
        if (!*prev || !*n) {
            if (buffer) {
                qemu_free(buffer);
                buffer = 0;
                buffersize = -1;
            }
            prev = 0;
            return 0;
        }
        s = prev;
    }
    for (; *n && strchr(delim, *s); s++, (*n)--);
    const char *e = s;
    for (; *n && *e && !strchr(delim, *e); e++, (*n)--);
    prev = e;
    if (buffersize < e - s) {
        buffersize = e - s;
        if (buffer) {
            qemu_free(buffer);
        }
        buffer = qemu_mallocz(buffersize + 1);
    }
    memcpy(buffer, s, e - s);
    buffer[e - s] = 0;
    if (real) {
        *real = (char *)s;
    }
    return buffer;
}

static void do_eglShaderPatch(char *source, int length)
{
    /* DISCLAIMER: this is not a full-blown shader parser but a simple
     * implementation which tries to remove the OpenGL ES shader
     * "precision" statements and precision qualifiers "lowp", "mediump"
     * and "highp" from the specified shader source. */
    if (!length) {
        length = strlen(source);
    }
    char *sp = 0;
    const char *p = opengl_strtok(source, &length, &sp);
    for (; p; p = opengl_strtok(0, &length, &sp)) {
        if (!strcmp(p, "lowp") || !strcmp(p, "mediump") || !strcmp(p, "highp")) {
            memset(sp, 0x20, strlen(p));
        } else if (!strcmp(p, "precision")) {
            while (*sp != ';') {
                *(sp++) = 0x20;
            }
            *sp = 0x20;
        }
    }
}

#define glShaderSource__(funcname) \
static void funcname##_(int handle, int size, char *prog, int *tab_length) \
{ \
    GET_EXT_PTR(void, funcname, (int, int, char **, void *)); \
    int i; \
    int acc_length = 0; \
    GLcharARB **tab_prog = malloc(size * sizeof(GLcharARB *)); \
    for (i = 0; i < size; i++) { \
        tab_prog[i] = ((GLcharARB *)prog) + acc_length; \
        if (tab_length[i]) { \
            acc_length += tab_length[i]; \
        } else { \
            acc_length += strlen(tab_prog[i]) + 1; \
        } \
    } \
    GLcharARB *host_prog = malloc(acc_length + 1); \
    memcpy(host_prog, (GLcharARB *)prog, acc_length); \
    host_prog[acc_length] = 0; \
    for (i = 0; i < size; i++) { \
        tab_prog[i] = host_prog + (tab_prog[i] - (GLcharARB *)prog); \
        do_eglShaderPatch(tab_prog[i], tab_length[i]); \
    } \
    ptr_func_##funcname(handle, size, tab_prog, tab_length); \
    free(host_prog); \
    free(tab_prog); \
}

glShaderSource__(glShaderSource);
glShaderSource__(glShaderSourceARB);

static int last_assigned_internal_num = 0;
static int last_active_internal_num = 0;

int do_function_call(struct helper_opengl_s *s, int func_number,
                     int pid, target_phys_addr_t *args, char *ret_string)
{
    char ret_char = 0;
    int ret_int = 0;
    const char *ret_str = NULL;
    int iProcess;
    ProcessStruct *process = NULL;
    
    for (iProcess = 0; iProcess < MAX_HANDLED_PROCESS; iProcess++) {
        if (processTab[iProcess].process_id == pid) {
            process = &processTab[iProcess];
            break;
        } else if (processTab[iProcess].process_id == 0) {
            process = &processTab[iProcess];
            memset(process, 0, sizeof(ProcessStruct));
            process->process_id = pid;
            process->internal_num = last_assigned_internal_num++;
            process->current_state = &process->default_state;
            break;
        }
    }
    if (process == NULL) {
        GL_ERROR("too many processes (got '%s' from pid %d)",
                 tab_opengl_calls_name[func_number], pid);
        return 0;
    }
    if (process->internal_num != last_active_internal_num) {
        glhost_makecurrent(process->current_state->drawable,
                           process->current_state->context);
        last_active_internal_num = process->internal_num;
    }
    
    process->instr_counter++;
    TRACE("[%d]> %s", process->instr_counter, tab_opengl_calls_name[func_number]);
    
    switch (func_number) {
        case _exit_process_func:
            doExitProcess(iProcess);
#ifndef QEMUGL_MULTITHREADED
            last_process_id = 0;
#endif
            break;
        case _createDrawable_func:
            doCreateDrawable(process, (int)args[0], (uint32_t)args[1],
                             (int)args[2], (int)args[3], (int)args[4]);
            break;
        case _resizeDrawable_func:
            doResizeDrawable(process, (int)args[0], (int)args[1],
                             (int)args[2], (uint32_t)args[3]);
            break;
        case glXWaitGL_func:
            glhost_xwaitgl();
            ret_int = 0;
            break;
        case glXChooseVisual_func:
            ret_int = doChooseVisual((int)args[0], (int *)args[1]);
            break;
        case glXCreateContext_func:
            ret_int = doCreateContext(process, (int)args[0], (int)args[1]);
            break;
        case glXCopyContext_func:
            doCopyContext(process, (int)args[0], (int)args[1],
                          (unsigned long)args[2]);
            break;
        case glXDestroyContext_func:
            doDestroyContext(process, (int)args[0]);
            break;
        case glXMakeCurrent_func:
            ret_int = doMakeCurrent(process, (int)args[0], (int)args[1]);
            break;
        case glXSwapBuffers_func:
            doSwapBuffers(s, process, (int)args[0]);
            break;
        case glXGetConfig_func:
            ret_int = glhost_getconfig(args[0]
                                       ? glhost_getvisualinfo(args[0])
                                       : glhost_getdefaultvisual(),
                                       (int)args[1], (int *)args[2]);
            break;
        case glXGetConfig_extended_func:
            doGetConfig_extended(process, (int)args[0], (int)args[1],
                                 (int *)args[2], (int *)args[3],
                                 (int *)args[4]);
            break;
        case glXDestroyPbuffer_func:
            doDestroyPbuffer(process, (int)args[0]);
            break;
        case glXSwapIntervalSGI_func:
            glhost_swapintervalSGI((int)args[0]);
            break;
        case glXGetProcAddress_fake_func:
            TRACE("glXGetProcAddress_fake: %s", (char *)args[0]);
            ret_int = glhost_getprocaddressARB((const GLubyte *)args[0]) != NULL;
            break;
        case glXGetProcAddress_global_fake_func:
            {
                int nbElts = args[0];
                char *huge_buffer = (char *)args[1];
                char *result = (char *)args[2];
                int i;
                for (i = 0; i < nbElts; i++) {
                    int len = strlen(huge_buffer);
                    result[i] = glhost_getprocaddressARB((const GLubyte*)huge_buffer) != NULL;
                    huge_buffer += len + 1;
                }
            }
            break;
        case glGetString_func:
            ret_str = (char *)glGetString((int)args[0]);
            break;
        case glShaderSourceARB_func:
            glShaderSourceARB_((int)args[0], (int)args[1], (char *)args[2],
                               (int *)args[3]);
            break;
        case glShaderSource_func:
            glShaderSource_((int)args[0], (int)args[1], (char *)args[2],
                            (int *)args[3]);
            break;
#if defined(WIN32) && !defined(USE_OSMESA)
        case glDrawRangeElements_func:
            {
                static void (*func)(GLenum, GLuint, GLuint, GLsizei, GLenum,
                                    const GLvoid *) = 0;
                if (!func) {
                    func = (void *)wglGetProcAddress("glDrawRangeElementsWIN");
                }
                if (func) {
                    func(args[0], args[1], args[2], args[3], args[4],
                         (void *)args[5]);
                }
            }
            break;
#endif
#ifdef SYSTEMATIC_ERROR_CHECK
        case glGetError_func:
            ret_int = process->current_state->last_error;
            break;
#endif
        default:
            execute_func(func_number, args, &ret_int, &ret_char);
            break;
    }
    
#ifdef SYSTEMATIC_ERROR_CHECK
    if (func_number == glGetError_func) {
        process->current_state->last_error = 0;
    } else {
        process->current_state->last_error = glGetError();
        if (process->current_state->last_error != 0) {
            GL_ERROR("error in %s: 0x%X\n",
                   tab_opengl_calls_name[func_number],
                   process->current_state->last_error);
        }
    }
#endif
    
    Signature *signature = (Signature *)tab_opengl_calls[func_number];
    int ret_type = signature->ret_type;
    //int nb_args = signature->nb_args;
    switch (ret_type) {
        case TYPE_NONE:
        case TYPE_INT:
        case TYPE_UNSIGNED_INT:
            break;
        case TYPE_CHAR:
        case TYPE_UNSIGNED_CHAR:
            ret_int = ret_char;
            break;
        case TYPE_CONST_CHAR:
            strncpy(ret_string, (ret_str) ? ret_str : "", 32768);
            break;
        default:
            GL_ERROR("unexpected ret type : %d", ret_type);
            exit(-1);
            break;
    }
    
    TRACE("[%d]< %s", process->instr_counter, tab_opengl_calls_name[func_number]);
    return ret_int;
}
