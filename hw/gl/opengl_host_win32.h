#ifndef OPENGL_HOST_WIN32_H__
#define OPENGL_HOST_WIN32_H__

#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>

//#define GLX_EXCESS_DEBUG

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", \
                                  __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef GLX_EXCESS_DEBUG
#define GLX_TRACE(...) GL_ERROR(__VA_ARGS__)
#else
#define GLX_TRACE(...)
#endif

#define GLX_USE_GL 1
#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6

static LPCTSTR search_libs_w32_[] = {NULL, "opengl32", (LPCTSTR)-1};

typedef PIXELFORMATDESCRIPTOR *GLHostVisualInfo;
typedef struct GLHostDrawable_s {
    HBITMAP bitmap;
    void *bitmap_addr;
    HGDIOBJ bitmap_defaultobj;
    int width, height, depth;
    HDC dc;
    PIXELFORMATDESCRIPTOR visual;
} *GLHostDrawable;
typedef struct GLHostContext_s {
	HDC dummy_dc;
	HGDIOBJ dummy_defaultobj;
	HBITMAP dummy_bitmap;
	HGLRC glrc;
} *GLHostContext; 
typedef GLHostDrawable GLHostPbuffer;

#include "opengl_host.h"

static struct glx_drawable_s {
    GLHostDrawable drawable;
    struct glx_drawable_s *next;
} *glx_drawables = 0;

static struct glx_visual_s {
    int visual_id;
    PIXELFORMATDESCRIPTOR *pf;
    struct glx_visual_s *next;
} *glx_visuals = 0;

static const char *glx_syserrmsg(void)
{
    static char *errmsg = 0;
    if (errmsg) {
        LocalFree(errmsg);
        errmsg = 0;
    }
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, GetLastError(), 0, (LPTSTR)&errmsg, 64, NULL);
    return errmsg;
}

static GLHostVisualInfo glx_visuals_add(int id, PIXELFORMATDESCRIPTOR *pf)
{
    struct glx_visual_s *p;
    if (!glx_visuals) {
        glx_visuals = qemu_mallocz(sizeof(struct glx_visual_s));
        p = glx_visuals;
    } else {
        for (p = glx_visuals; p->next; p = p->next) {
            if (p->pf == pf) {
                return pf;
            } else if (p->visual_id == id) {
                qemu_free(pf);
                return p->pf;
            }
        }
        p->next = qemu_mallocz(sizeof(struct glx_visual_s));
        p = p->next;
    }
    p->visual_id = id;
    p->pf = pf;
    p->next = 0;
    
    return pf;
}

GLHostVisualInfo glhost_getvisualinfo(int vis_id)
{
    GLX_TRACE("vis_id=%d", vis_id);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->visual_id == vis_id) {
            return p->pf;
        }
    }
    return 0;
}

GLHostVisualInfo glhost_getdefaultvisual(void)
{
	PIXELFORMATDESCRIPTOR *pf = NULL;
    HDC dc = GetDC(NULL);
    int idx = GetPixelFormat(dc);
    if (!idx) {
        GL_ERROR("GetPixelFormat failed: %s", glx_syserrmsg());
    } else { 
        GLX_TRACE("default pixel format id=%d", idx);
        pf = qemu_mallocz(sizeof(PIXELFORMATDESCRIPTOR));
        if (DescribePixelFormat(dc, idx, sizeof(PIXELFORMATDESCRIPTOR), pf)) {
            pf = glx_visuals_add(idx, pf);
        } else {
            qemu_free(pf);
            pf = NULL;
            GL_ERROR("DescribePixelFormat failed: %s", glx_syserrmsg());
        }
    }
    ReleaseDC(NULL, dc);
    return pf;
}

int glhost_idforvisual(GLHostVisualInfo vis)
{
    GLX_TRACE("visual=%p", vis);
    int vis_id = 0;
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->pf == vis) {
            vis_id = p->visual_id;
            break;
        }
    }
    if (vis_id) {
        return vis_id;
    }
    GL_ERROR("oops - unknown visual %p", vis);
    exit(-1);
    return 0;
}

GLHostVisualInfo glhost_choosevisual(int depth, const int *attribs)
{
    PIXELFORMATDESCRIPTOR *vis = qemu_mallocz(sizeof(PIXELFORMATDESCRIPTOR));
    vis->nSize = sizeof(PIXELFORMATDESCRIPTOR);
    vis->nVersion = 1;
    vis->dwFlags = PFD_DRAW_TO_BITMAP |
                   PFD_SUPPORT_OPENGL |
                   PFD_SUPPORT_GDI |
                   PFD_GENERIC_ACCELERATED;
    vis->iLayerType = PFD_MAIN_PLANE;
    vis->iPixelType = PFD_TYPE_RGBA;
    vis->cColorBits = depth;
    vis->cAlphaBits = 0;
    vis->cDepthBits = 16;
    HDC dc = GetDC(NULL);
    int idx = ChoosePixelFormat(dc, vis);
    if (!idx) {
        GL_ERROR("ChoosePixelFormat failed: %s", glx_syserrmsg());
    } else {
        if (DescribePixelFormat(dc, idx, sizeof(PIXELFORMATDESCRIPTOR), vis)) {
#ifdef GLX_EXCESS_DEBUG
            GLX_TRACE("nearest matching pixel format id=%d, attribs:", idx);
            char *flags = qemu_mallocz(4096);
            if (vis->dwFlags & PFD_DRAW_TO_WINDOW)        strcat(flags, "PFD_DRAW_TO_WINDOW ");
            if (vis->dwFlags & PFD_DRAW_TO_BITMAP)        strcat(flags, "PFD_DRAW_TO_BITMAP ");
            if (vis->dwFlags & PFD_SUPPORT_GDI)           strcat(flags, "PFD_SUPPORT_GDI ");
            if (vis->dwFlags & PFD_SUPPORT_OPENGL)        strcat(flags, "PFD_SUPPORT_OPENGL ");
            if (vis->dwFlags & PFD_GENERIC_ACCELERATED)   strcat(flags, "PFD_GENERIC_ACCELERATED ");
            if (vis->dwFlags & PFD_GENERIC_FORMAT)        strcat(flags, "PFD_GENERIC_FORMAT ");
            if (vis->dwFlags & PFD_NEED_PALETTE)          strcat(flags, "PFD_NEED_PALETTE ");
            if (vis->dwFlags & PFD_NEED_SYSTEM_PALETTE)   strcat(flags, "PFD_NEED_SYSTEM_PALETTE ");
            if (vis->dwFlags & PFD_DOUBLEBUFFER)          strcat(flags, "PFD_DOUBLEBUFFER ");
            if (vis->dwFlags & PFD_STEREO)                strcat(flags, "PFD_STEREO ");
            if (vis->dwFlags & PFD_SWAP_LAYER_BUFFERS)    strcat(flags, "PFD_SWAP_LAYER_BUFFERS ");
            if (vis->dwFlags & PFD_DEPTH_DONTCARE)        strcat(flags, "PFD_DEPTH_DONTCARE ");
            if (vis->dwFlags & PFD_DOUBLEBUFFER_DONTCARE) strcat(flags, "PFD_DOUBLEBUFFER_DONTCARE ");
            if (vis->dwFlags & PFD_STEREO_DONTCARE)       strcat(flags, "PFD_STEREO_DONTCARE ");
            if (vis->dwFlags & PFD_SWAP_COPY)             strcat(flags, "PFD_SWAP_COPY ");
            if (vis->dwFlags & PFD_SWAP_EXCHANGE)         strcat(flags, "PFD_SWAP_EXCHANGE ");
            GLX_TRACE("- dwFlags = 0x%08x (%s)", vis->dwFlags, flags);
            qemu_free(flags);
            GLX_TRACE("- iPixelType = %s", vis->iPixelType == PFD_TYPE_RGBA 
                                           ? "PFD_TYPE_RGBA"
                                           : "PFD_TYPE_COLORINDEX");
            GLX_TRACE("- cColorBits = %d", vis->cColorBits);
            GLX_TRACE("- cRedBits = %d", vis->cRedBits);
            GLX_TRACE("- cRedShift = %d", vis->cRedShift);
            GLX_TRACE("- cGreenBits = %d", vis->cGreenBits);
            GLX_TRACE("- cGreenShift = %d", vis->cGreenShift);
            GLX_TRACE("- cBlueBits = %d", vis->cBlueBits);
            GLX_TRACE("- cBlueShift = %d", vis->cBlueShift);
            GLX_TRACE("- cAlphaBits = %d", vis->cAlphaBits);
            GLX_TRACE("- cAlphaShift = %d", vis->cAlphaShift);
            GLX_TRACE("- cAccumBits = %d", vis->cAccumBits);
            GLX_TRACE("- cAccumRedBits = %d", vis->cAccumRedBits);
            GLX_TRACE("- cAccumGreenBits = %d", vis->cAccumGreenBits);
            GLX_TRACE("- cAccumBlueBits = %d", vis->cAccumBlueBits);
            GLX_TRACE("- cAccumAlphaBits = %d", vis->cAccumAlphaBits);
            GLX_TRACE("- cDepthBits = %d", vis->cDepthBits);
            GLX_TRACE("- cStencilBits = %d", vis->cStencilBits);
            GLX_TRACE("- cAuxBuffers = %d", vis->cAuxBuffers);
            GLX_TRACE("- dwVisibleMask = 0x%08x", vis->dwVisibleMask);
#endif
            vis = glx_visuals_add(idx, vis);
        } else {
            GL_ERROR("DescribePixelFormat failed: %s", glx_syserrmsg());
            qemu_free(vis);
            vis = 0;
        }
    }
    ReleaseDC(NULL, dc);
    return vis;
}

static int glx_createdrawable_buffer(GLHostDrawable drawable)
{
	int result = 0;
	drawable->dc = CreateCompatibleDC(NULL);
	if (drawable->dc == NULL) {
        GL_ERROR("CreateCompatibleDC failed: %s", glx_syserrmsg());
    } else {
        BITMAPINFO binfo;
        memset(&binfo, 0, sizeof(binfo));
        binfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        binfo.bmiHeader.biWidth = drawable->width;
        binfo.bmiHeader.biHeight = drawable->height;
        binfo.bmiHeader.biPlanes = 1;
        binfo.bmiHeader.biBitCount = drawable->depth;
        binfo.bmiHeader.biCompression = BI_RGB;
        drawable->bitmap = CreateDIBSection(drawable->dc, &binfo,
                                            DIB_PAL_COLORS,
                                            &drawable->bitmap_addr,
                                            NULL, 0);
        if (drawable->bitmap == NULL) {
            GL_ERROR("CreateDIBSection failed: %s", glx_syserrmsg());
        } else {
            drawable->bitmap_defaultobj = SelectObject(drawable->dc,
                                                       drawable->bitmap);
            if (drawable->bitmap_defaultobj == NULL ||
                drawable->bitmap_defaultobj == HGDI_ERROR) {
                GL_ERROR("SelectObject failed: %s", glx_syserrmsg());
            } else {
                GLX_TRACE("created new GLXDrawable %p [bitmap=0x%08x, dc=0x%08x]",
                          drawable, drawable->bitmap, drawable->dc);
                if (!SetPixelFormat(drawable->dc,
                                    ChoosePixelFormat(drawable->dc,
                                                      &drawable->visual),
                                    &drawable->visual)) {
                    GL_ERROR("SetPixelFormat failed: %s", glx_syserrmsg());
                    SelectObject(drawable->dc, drawable->bitmap_defaultobj);
                } else {
                    result = 1;
                }
            }
            if (!result) {
                DeleteObject(drawable->bitmap);
                drawable->bitmap = 0;
            }
        }
        if (!result) {
            DeleteDC(drawable->dc);
            drawable->dc = 0;
        }
    }
    return result;
}

GLHostDrawable glhost_createdrawable(GLHostVisualInfo vis, int width,
                                     int height, int depth)
{
	GLX_TRACE("size = %dx%d, depth = %d", width, height, depth);
    GLHostDrawable drawable = qemu_mallocz(sizeof(struct GLHostDrawable_s));
    memcpy(&drawable->visual, vis, sizeof(drawable->visual));
    drawable->width = width;
    drawable->height = height;
    drawable->depth = depth;
    if (!glx_createdrawable_buffer(drawable)) {
        qemu_free(drawable);
        drawable = 0;
    } else {
        struct glx_drawable_s *d;
        if (!glx_drawables) {
            glx_drawables = qemu_mallocz(sizeof(struct glx_drawable_s));
            d = glx_drawables;
        } else {
            for (d = glx_drawables; d->next; d = d->next) {
            }
            d->next = qemu_mallocz(sizeof(struct glx_drawable_s));
            d = d->next;
        }
        d->next = 0;
        d->drawable = drawable;
    }
    return drawable;
}

static void glx_destroydrawable_buffer(GLHostDrawable drawable)
{
    GLX_TRACE("destroying bitmap for drawable %p [bitmap=0x%08x, dc=0x%08x]",
              drawable, drawable->bitmap, drawable->dc); 
    SelectObject(drawable->dc, drawable->bitmap_defaultobj);
    if (!DeleteObject(drawable->bitmap)) {
        GL_ERROR("DeleteObject failed: %s", glx_syserrmsg());
    } else {
        drawable->bitmap = 0;
    }
    drawable->bitmap_addr = 0;
    if (!DeleteDC(drawable->dc)) {
        GL_ERROR("DeleteDC failed: %s", glx_syserrmsg());
    } else {
        drawable->dc = 0;
    }
}

void glhost_destroydrawable(GLHostDrawable drawable)
{
	glx_destroydrawable_buffer(drawable);
    GLX_TRACE("releasing dc for drawable %p [bitmap=0x%08x, dc=0x%08x]",
              drawable, drawable->bitmap, drawable->dc);
    if (!DeleteDC(drawable->dc)) {
        GL_ERROR("DeleteDC failed: %s", glx_syserrmsg());
    }
    drawable->dc = 0;
    GLX_TRACE("destroying drawable %p [bitmap=0x%08x, dc=0x%08x]",
              drawable, drawable->bitmap, drawable->dc);
    struct glx_drawable_s *p = glx_drawables;
    if (drawable == p->drawable) {
        glx_drawables = p->next;
    } else {
        for (; p && p->next; p = p->next) {
            if (p->next->drawable == drawable) {
                struct glx_drawable_s *q = p->next;
                p->next = p->next->next;
                p = q;
                break;
            }
        }
        if (!p || p->drawable != drawable) {
            GL_ERROR("unable to find drawable %p in drawables list", drawable);
            exit(-1);
        }
    }
    qemu_free(drawable);
    p->drawable = 0;
    p->next = 0;
    qemu_free(p);
}

void glhost_resizedrawable(GLHostDrawable drawable, int width, int height)
{
	GLX_TRACE("drawable %p, new size %dx%d", drawable, width, height);
    if (drawable && (drawable->width != width || drawable->height != height)) {
        HGLRC context = wglGetCurrentContext();
        int reassign = (wglGetCurrentDC() == drawable->dc);
        if (context && reassign) {
            if (!wglMakeCurrent(drawable->dc, NULL)) {
                GL_ERROR("wglMakeCurrent failed: %s", glx_syserrmsg());
            }
        }
        glx_destroydrawable_buffer(drawable);
        drawable->width = width;
        drawable->height = height;
        if (glx_createdrawable_buffer(drawable) && context && reassign) {
            if (!wglMakeCurrent(drawable->dc, context)) {
                GL_ERROR("wglMakeCurrent failed: %s", glx_syserrmsg());
            }
        }
    }
}

void *glhost_swapbuffers(GLHostDrawable drawable)
{
	if (drawable) {
        glFlush();
        GdiFlush();
        return drawable->bitmap_addr;
    }
    return 0;
}

GLHostContext glhost_createcontext(GLHostVisualInfo vis, GLHostContext share)
{
    GLX_TRACE("vis=%p, share=0x%x", vis, share);
    GLHostContext ctx = (GLHostContext)qemu_mallocz(sizeof(*ctx));
    int vis_id = glhost_idforvisual(vis);
    ctx->dummy_dc = CreateCompatibleDC(NULL);
    if (ctx->dummy_dc == NULL) {
        GL_ERROR("CreateCompatibleDC failed: %s", glx_syserrmsg());
        qemu_free(ctx);
        ctx = 0;
    } else {
        BITMAPINFO binfo;
        memset(&binfo, 0, sizeof(binfo));
        binfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        binfo.bmiHeader.biWidth = 1;
        binfo.bmiHeader.biHeight = 1;
        binfo.bmiHeader.biPlanes = 1;
        binfo.bmiHeader.biBitCount = vis->cColorBits;
        binfo.bmiHeader.biCompression = BI_RGB;
        void *ptr = 0;
        ctx->dummy_bitmap = CreateDIBSection(ctx->dummy_dc, &binfo,
                                             DIB_PAL_COLORS, &ptr, NULL, 0);
        if (ctx->dummy_bitmap == NULL) {
            GL_ERROR("CreateDIBSection failed: %s", glx_syserrmsg());
            DeleteDC(ctx->dummy_dc);
            qemu_free(ctx);
            ctx = 0;
        } else {
            HGDIOBJ old = SelectObject(ctx->dummy_dc, ctx->dummy_bitmap);
            if (old == NULL || old == HGDI_ERROR) {
                GL_ERROR("SelectObject failed: %s", glx_syserrmsg());
                DeleteObject(ctx->dummy_bitmap);
                DeleteDC(ctx->dummy_dc);
                qemu_free(ctx);
                ctx = 0;
            } else {
            	ctx->dummy_defaultobj = old;
                if (GetPixelFormat(ctx->dummy_dc) != vis_id) {
                    if (!SetPixelFormat(ctx->dummy_dc, vis_id, vis)) {
                        GL_ERROR("SetPixelFormat failed: %s", glx_syserrmsg());
                        SelectObject(ctx->dummy_dc, ctx->dummy_defaultobj);
                        DeleteObject(ctx->dummy_bitmap);
                        DeleteDC(ctx->dummy_dc);
                        qemu_free(ctx);
                        ctx = 0;
                    }
                }
                if (ctx) {
                    ctx->glrc = wglCreateContext(ctx->dummy_dc);
                    if (!ctx->glrc) {
                        GL_ERROR("wglCreateContext failed: %s", glx_syserrmsg());
                        SelectObject(ctx->dummy_dc, ctx->dummy_defaultobj);
                        DeleteObject(ctx->dummy_bitmap);
                        DeleteDC(ctx->dummy_dc);
                        qemu_free(ctx);
                        ctx = 0;
                    } else {
                        GLX_TRACE("created glxcontext 0x%x", ctx);
                        if (share) {
                            wglShareLists(ctx->glrc, share->glrc);
                        }
                    }
                }
            }
        }
    }
    return ctx;
}

void glhost_copycontext(GLHostContext src, GLHostContext dst,
                        unsigned long mask)
{
    GLX_TRACE("src=%p, dst=%p, mask=0x%lx", src, dst, mask);
    wglCopyContext(src->glrc, dst->glrc, mask);
}

int glhost_makecurrent(GLHostDrawable drawable, GLHostContext context)
{
    GLX_TRACE("drawable=%p [bmp=0x%x, dc=0x%x], context=0x%08x",
              drawable,
              drawable ? drawable->bitmap : 0,
              drawable ? drawable->dc : 0,
              context);
    HDC dc = drawable ? drawable->dc : wglGetCurrentDC();
    int result = wglMakeCurrent(dc, context ? context->glrc : NULL);
    if (dc && !result) {
        GL_ERROR("wglMakeCurrent failed: %s", glx_syserrmsg());
    }
    return result;
}

void glhost_destroycontext(GLHostContext context)
{
    GLX_TRACE("context=0x%08x", context);
    if (context) {
        if (context->glrc) {
            if (wglDeleteContext(context->glrc) == FALSE) {
                GL_ERROR("wglDeleteContext failed: %s", glx_syserrmsg());
            }
            context->glrc = 0;
        }
        if (context->dummy_dc) {
            if (context->dummy_defaultobj) {
        	    SelectObject(context->dummy_dc, context->dummy_defaultobj);
                context->dummy_defaultobj = 0;
            }
            if (context->dummy_bitmap) {
                if (!DeleteObject(context->dummy_bitmap)) {
                    GL_ERROR("DeleteObject failed: %s", glx_syserrmsg());
                }
                context->dummy_bitmap = 0;
            }
            if (!DeleteDC(context->dummy_dc)) {
                GL_ERROR("DeleteDC failed: %s", glx_syserrmsg());
            }
            context->dummy_dc = 0;
        }
        qemu_free(context);
    }
}

void glhost_destroypbuffer(GLHostPbuffer pbuffer)
{
	GLX_TRACE("pbuffer=%p", pbuffer);
	glhost_destroydrawable((GLHostDrawable)pbuffer);
}

void glhost_xwaitgl(void)
{
    glFinish();
}

int glhost_getconfig(GLHostVisualInfo vis, int attrib, int *value)
{
    switch (attrib) {
        case 1: /* GLX_USE_GL */
            *value = 1;
            break;
        case 2: /* GLX_BUFFER_SIZE */
            *value = vis->cColorBits;
            break;
        case 3: /* GLX_LEVEL */
            *value = 0;
            break;
        case 4: /* GLX_RGBA */
            *value = 1;
            break;
        case 5: /* GLX_DOUBLEBUFFER */
            *value = (vis->dwFlags & PFD_DOUBLEBUFFER) ? 1 : 0;
            break;
        case 6: /* GLX_STEREO */
            *value = (vis->dwFlags & PFD_STEREO) ? 1 : 0;
            break;
        case 7: /* GLX_AUX_BUFFERS */
            *value = vis->cAuxBuffers;
            break;
        case 8: /* GLX_RED_SIZE */
            *value = vis->cRedBits;
            break;
        case 9: /* GLX_GREEN_SIZE */
            *value = vis->cGreenBits;
            break;
        case 10: /* GLX_BLUE_SIZE */
            *value = vis->cBlueBits;
            break;
        case 11: /* GLX_ALPHA_SIZE */
            *value = vis->cAlphaBits;
            break;
        case 12: /* GLX_DEPTH_SIZE */
            *value = vis->cDepthBits;
            break;
        case 13: /* GLX_STENCIL_SIZE */
            *value = vis->cStencilBits;
            break;
        case 14: /* GLX_ACCUM_RED_SIZE */
            *value = vis->cAccumRedBits;
            break;
        case 15: /* GLX_ACCUM_GREEN_SIZE */
            *value = vis->cAccumGreenBits;
            break;
        case 16: /* GLX_ACCUM_BLUE_SIZE */
            *value = vis->cAccumBlueBits;
            break;
        case 17: /* GLX_ACCUM_ALPHA_SIZE */
            *value = vis->cAccumAlphaBits;
            break;
        case 32: /* GLX_CONFIG_CAVEAT */
            *value = 0x8000; /* GLX_NONE */
            break;
        case 34: /* GLX_X_VISUAL_TYPE */
            *value = (vis->iPixelType == PFD_TYPE_RGBA)
                     ? (vis->cRedBits > 7 && 
                        vis->cGreenBits > 7 && 
                        vis->cBlueBits > 7)
                        ? 0x8002 /* GLX_TRUE_COLOR */
                        : 0x8003 /* GLX_DIRECT_COLOR */
                     : 0x8004;   /* GLX_PSEUDO_COLOR */
            break;
        case 35: /* GLX_TRANSPARENT_TYPE */
            switch (vis->iPixelType) {
                case PFD_TYPE_RGBA:
                    *value = 0x8008; /* GLX_TRANSPARENT_RGB */
                    break;
                case PFD_TYPE_COLORINDEX:
                    *value = 0x8009; /* GLX_TRANSPARENT_INDEX */
                    break;
                default:
                    *value = 0x8000; /* GLX_NONE */
                    break;
            }
            break;
        case 36: /* GLX_TRANSPARENT_INDEX_VALUE */
            *value = vis->dwVisibleMask;
            break;
        case 37: /* GLX_TRANSPARENT_RED_VALUE */
            *value = (vis->dwVisibleMask >> vis->cRedShift) & ((1 << vis->cRedBits) - 1);
            break;
        case 38: /* GLX_TRANSPARENT_GREEN_VALUE */
            *value = (vis->dwVisibleMask >> vis->cGreenShift) & ((1 << vis->cGreenBits) - 1);
            break;
        case 39: /* GLX_TRANSPARENT_BLUE_VALUE */
            *value = (vis->dwVisibleMask >> vis->cBlueShift) & ((1 << vis->cBlueBits) - 1);
            break;
        case 40: /* GLX_TRANSPARENT_ALPHA_VALUE */
            *value = (vis->dwVisibleMask >> vis->cAlphaShift) & ((1 << vis->cAlphaBits) - 1);
            break;
        case 100000: /* GLX_SAMPLE_BUFFERS */
            *value = 0;
            break;
        case 100001: /* GLX_SAMPLES */
            *value = 0;
            break;
        case 0x20b0: /* GLX_FLOAT_COMPONENTS_NV */
            *value = 0;
            break;
        case 0x8001: /* GLX_SLOW_CONFIG */
        case 0x8005: /* GLX_STATIC_COLOR */
        case 0x8006: /* GLX_GRAY_SCALE */
        case 0x8007: /* GLX_STATIC_GRAY */
        case 0x8013: /* GLX_FBCONFIG_ID */
        case 0x801b: /* GLX_PRESERVED_CONTENTS */
            *value = 0;
            break;
        case 0x8002: /* GLX_TRUE_COLOR */
        case 0x8003: /* GLX_DIRECT_COLOR */
        case 0x8004: /* GLX_PSEUDO_COLOR */
        case 0x8008: /* GLX_TRANSPARENT_RGB */
        case 0x8009: /* GLX_TRANSPARENT_INDEX */
        case 0x8012: /* GLX_X_RENDERABLE */
        case 0x8014: /* GLX_RGBA_TYPE */
        case 0x8015: /* GLX_COLOR_INDEX_TYPE */
            *value = 1;
            break;
        case 0x800b: /* GLX_VISUAL_ID */
            *value = glhost_idforvisual(vis);
            break;
        case 0x8010: /* GLX_DRAWABLE_TYPE */
            *value = 0x5; /* GLX_WINDOW_BIT | GLX_PBUFFER_BIT */
            break;
        case 0x8011: /* GLX_RENDER_TYPE */
            *value = 0x3; /* GLX_RGBA_BIT | GLX_COLOR_INDEX_BIT */
            break;
        case 0x8016: /* GLX_MAX_PBUFFER_WIDTH */
        case 0x8017: /* GLX_MAX_PBUFFER_HEIGHT */
            *value = 2048;
            break;
        case 0x8018: /* GLX_MAX_PBUFFER_PIXELS */
            *value = 2048 * 2048;
            break;
        default:
            GL_ERROR("oops - unsupported attribute %d", attrib);
            return 2; /* GLX_BAD_ATTRIBUTE */
    }
    return 0; /* success */
}

void *glhost_getprocaddressARB(const unsigned char *name)
{
    void *f = wglGetProcAddress(name);
    int i_;
    for (i_ = 0; !f && search_libs_w32_[i_] != (LPCTSTR)-1; i_++) {
        HMODULE hmod = GetModuleHandle(search_libs_w32_[i_]);
        if (!hmod && search_libs_w32_[i_]) {
            hmod = LoadLibrary(search_libs_w32_[i_]);
            /* never to be released again... */
        }
        if (hmod) {
            f = (void *)GetProcAddress(hmod, TEXT(name));
        }
    }
    return f;
}

int glhost_swapintervalSGI(int interval)
{
    /* ignore, anything goes */
    return 0;
}

#endif
