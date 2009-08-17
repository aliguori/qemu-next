#ifndef OPENGL_HOST_OSMESA_H__
#define OPENGL_HOST_OSMESA_H__

#include <GL/osmesa.h>

typedef struct GLHostVisualInfo_s {
    int id;
    int depth;
    int redsize;
    int greensize;
    int bluesize;
} *GLHostVisualInfo;
typedef OSMesaContext GLHostContext;
typedef struct GLHostDrawable_s {
    int width, height, depth;
    unsigned char *buffer;
} *GLHostDrawable;
typedef void *GLHostPbuffer;

#define GLX_USE_GL 1
#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6

#include "opengl_host.h"

#define GLX_EXCESS_DEBUG

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", \
                                  __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef GLX_EXCESS_DEBUG
#define GLX_TRACE(...) GL_ERROR(__VA_ARGS__)
#else
#define GLX_TRACE(...)
#endif

static struct glx_visual_s {
    GLHostVisualInfo visual;
    struct glx_visual_s *next;
} *glx_visuals = 0;

static GLHostVisualInfo glx_visuals_add(GLHostVisualInfo visual)
{
    GLX_TRACE("visual %p", visual);
    struct glx_visual_s *p;
    if (!glx_visuals) {
        glx_visuals = malloc(sizeof(struct glx_visual_s));
        p = glx_visuals;
    } else {
        for (p = glx_visuals; p->next; p = p->next) {
		    if (p->visual == visual) {
                return visual;
		    } else if (p->visual->id == visual->id) {
                GLX_TRACE("already cached as %p", p->visual);
                qemu_free(visual);
                return p->visual;
            }
        }
        p->next = malloc(sizeof(struct glx_visual_s));
        p = p->next;
    }
    GLX_TRACE("adding new visual %p with id 0x%x", visual, visual->id);
    p->visual = visual;
    p->next = 0;
    
    return p->visual;
}

static GLHostVisualInfo glx_visualfordepth(int depth)
{
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->visual->depth == depth) {
            return p->visual;
        }
    }
    return 0;
}

GLHostVisualInfo glhost_getvisualinfo(int vis_id)
{
    GLX_TRACE("vis_id = 0x%x", vis_id);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->visual->id == vis_id) {
            return p->visual;
        }
    }
    GL_ERROR("unknown visual id 0x%x", vis_id);
    return 0;
}

GLHostVisualInfo glhost_getdefaultvisual(void)
{
    GLX_TRACE("searching for default visual...");
    return glhost_choosevisual(32, NULL);
}

int glhost_idforvisual(GLHostVisualInfo vis)
{
    GLX_TRACE("vis = %p", vis);
    return vis->id;
}

GLHostVisualInfo glhost_choosevisual(int depth, const int *attribs)
{
    static int next_free_id = 1;
#ifdef GLX_OSMESA_FORCE_32BPP
    GLX_TRACE("depth=%d --> forced to 32", depth);
    depth=32;
#else
    GLX_TRACE("depth=%d",depth);
#endif
    GLHostVisualInfo vis = glx_visualfordepth(depth);
    if (!vis) {
        vis = qemu_mallocz(sizeof(*vis));
        vis->id = next_free_id++;
        vis->depth = depth;
        switch (depth) {
#if OSMESA_MAJOR_VERSION >= 2
            case 16:
                vis->redsize = 5;
                vis->greensize = 6;
                vis->bluesize = 5;
                break;
            case 24:
#endif
            case 32:
                vis->redsize = 8;
                vis->greensize = 8;
                vis->bluesize = 8;
                break;
            default:
                GL_ERROR("unsupported depth %d", depth);
                break;
        }
        vis = glx_visuals_add(vis);
    }
    return vis;
}

static void glx_createdrawable_buffer(GLHostDrawable d)
{
    GLX_TRACE("%dx%d, depth %d (%d bytes/pixel)", d->width, d->height,
			  d->depth, (d->depth + 7) / 8);
    int size = d->width * d->height * ((d->depth + 7) / 8);
    d->buffer = qemu_mallocz(size);
	GLX_TRACE("created buffer (size %d) at %p", size, d->buffer);
}

static void glx_destroydrawable_buffer(GLHostDrawable d)
{
    qemu_free(d->buffer);
    d->buffer = 0;
}

GLHostDrawable glhost_createdrawable(GLHostVisualInfo vis, int width,
                                     int height, int depth)
{
#ifdef GLX_OSMESA_FORCE_32BPP
    GLX_TRACE("vis %p, size %dx%d, depth %d --> forced to 32bpp", vis, width,
              height, depth);
    depth = 32;
#else
    GLX_TRACE("vis %p, size %dx%d, depth %d", vis, width, height, depth);
#endif
    GLHostDrawable d = (GLHostDrawable)qemu_mallocz(sizeof(*d));
    d->width = width;
    d->height = height;
    d->depth = depth;
    glx_createdrawable_buffer(d);
    GLX_TRACE("created drawable %p", d);
    return d;
}

void glhost_destroydrawable(GLHostDrawable drawable)
{
    GLX_TRACE("drawable %p", drawable);
    glx_destroydrawable_buffer(drawable);
    qemu_free(drawable);
}

void glhost_resizedrawable(GLHostDrawable drawable, int width, int height)
{
    GLX_TRACE("drawable %p, new size %dx%d", drawable, width, height);
    OSMesaContext context = OSMesaGetCurrentContext();
    glx_destroydrawable_buffer(drawable);
    drawable->width = width;
    drawable->height = height;
    glx_createdrawable_buffer(drawable);
    if (context) {
        OSMesaMakeCurrent(context, drawable->buffer, GL_UNSIGNED_BYTE,
                          drawable->width, drawable->height);
    }
}

void *glhost_swapbuffers(GLHostDrawable drawable)
{
    glFinish();
    return drawable->buffer;
}

GLHostContext glhost_createcontext(GLHostVisualInfo vis, GLHostContext share)
{
    GLX_TRACE("vis=%p, share=%p", vis, share);
	GLHostContext context = 0;
#if OSMESA_MAJOR_VERSION >= 2
    switch (vis->depth) {
#if (OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION) >= 305
        case 16: context = OSMesaCreateContextExt(OSMESA_RGB_565, 16, 0, 0, share); break;
        case 24: context = OSMesaCreateContextExt(OSMESA_RGB, 16, 0, 0, share); break;
        case 32: context = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, share); break;
#else
        case 16: context = OSMesaCreateContext(OSMESA_RGB_565, share); break;
        case 24: context = OSMesaCreateContext(OSMESA_RGB, share); break;
        case 32: context = OSMesaCreateContext(OSMESA_RGBA, share); break;
#endif
        default: GL_ERROR("unsupported depth %d", vis->depth); break;
    }
#else
	context = OSMesaCreateContext(OSMESA_RGBA, share);
#endif
	GLX_TRACE("created context %p", context);
	return context;
}

void glhost_copycontext(GLHostContext src, GLHostContext dst,
                        unsigned long mask)
{
    GLX_TRACE("src=%p, dst=%p, mask=0x%lx", src, dst, mask);
    GL_ERROR("not implemented");
}

int glhost_makecurrent(GLHostDrawable drawable, GLHostContext context)
{
    GLX_TRACE("drawable=%p, context %p", drawable, context);
    return OSMesaMakeCurrent(context, drawable ? drawable->buffer : 0,
							 GL_UNSIGNED_BYTE, drawable ? drawable->width : 0,
							 drawable ? drawable->height : 0);
}

void glhost_destroycontext(GLHostContext context)
{
    GLX_TRACE("context %p", context);
    OSMesaDestroyContext(context);
}

void glhost_destroypbuffer(GLHostPbuffer pbuffer)
{
    GLX_TRACE("pbuffer %p", pbuffer);
    GL_ERROR("not implemented");
}

void glhost_xwaitgl(void)
{
    glFlush();
}

int glhost_getconfig(GLHostVisualInfo vis, int attrib, int *value)
{
    switch (attrib) {
        case 1: /* GLX_USE_GL */
        case 4: /* GLX_RGBA */
        case 0x8002: /* GLX_TRUE_COLOR */
        case 0x8012: /* GLX_X_RENDERABLE */
        case 0x8014: /* GLX_RGBA_TYPE */
            *value = 1;
            break;
        case 2: /* GLX_BUFFER_SIZE */
            *value = vis->depth;
            break;
        case 3: /* GLX_LEVEL */
        case 5: /* GLX_DOUBLEBUFFER */
        case 6: /* GLX_STEREO */
        case 7: /* GLX_AUX_BUFFERS */
        case 13: /* GLX_STENCIL_SIZE */
        case 14: /* GLX_ACCUM_RED_SIZE */
        case 15: /* GLX_ACCUM_GREEN_SIZE */
        case 16: /* GLX_ACCUM_BLUE_SIZE */
        case 17: /* GLX_ACCUM_ALPHA_SIZE */
        case 36: /* GLX_TRANSPARENT_INDEX_VALUE */
        case 37: /* GLX_TRANSPARENT_RED_VALUE */
        case 38: /* GLX_TRANSPARENT_GREEN_VALUE */
        case 39: /* GLX_TRANSPARENT_BLUE_VALUE */
        case 40: /* GLX_TRANSPARENT_ALPHA_VALUE */
        case 100000: /* GLX_SAMPLE_BUFFERS */
        case 100001: /* GLX_SAMPLES */
        case 0x20b0: /* GLX_FLOAT_COMPONENTS_NV */
        case 0x8001: /* GLX_SLOW_CONFIG */
        case 0x8003: /* GLX_DIRECT_COLOR */
        case 0x8004: /* GLX_PSEUDO_COLOR */
        case 0x8005: /* GLX_STATIC_COLOR */
        case 0x8006: /* GLX_GRAY_SCALE */
        case 0x8007: /* GLX_STATIC_GRAY */
        case 0x8008: /* GLX_TRANSPARENT_RGB */
        case 0x8009: /* GLX_TRANSPARENT_INDEX */
        case 0x8013: /* GLX_FBCONFIG_ID */
        case 0x8015: /* GLX_COLOR_INDEX_TYPE */
        case 0x8016: /* GLX_MAX_PBUFFER_WIDTH */
        case 0x8017: /* GLX_MAX_PBUFFER_HEIGHT */
        case 0x8018: /* GLX_MAX_PBUFFER_PIXELS */
        case 0x801b: /* GLX_PRESERVED_CONTENTS */
            *value = 0;
            break;
        case 8: /* GLX_RED_SIZE */
            *value = vis->redsize;
            break;
        case 9: /* GLX_GREEN_SIZE */
            *value = vis->greensize;
            break;
        case 10: /* GLX_BLUE_SIZE */
            *value = vis->bluesize;
            break;
        case 11: /* GLX_ALPHA_SIZE */
            *value = 0;
            break;
        case 12: /* GLX_DEPTH_SIZE */
            *value = 16;
            break;
        case 32: /* GLX_CONFIG_CAVEAT */
        case 35: /* GLX_TRANSPARENT_TYPE */
            *value = 0x8000; /* GLX_NONE */
            break;
        case 34: /* GLX_X_VISUAL_TYPE */
            *value = 0x8002; /* GLX_TRUE_COLOR */
            break;
        case 0x800b: /* GLX_VISUAL_ID */
            *value = glhost_idforvisual(vis);
            break;
        case 0x8010: /* GLX_DRAWABLE_TYPE */
            *value = 0x1; /* GLX_WINDOW_BIT */
            break;
        case 0x8011: /* GLX_RENDER_TYPE */
            *value = 0x1; /* GLX_RGBA_BIT */
            break;
        default:
            GL_ERROR("oops - unsupported attribute 0x%x", attrib);
            return 2; /* GLX_BAD_ATTRIBUTE */
    }
    return 0; /* success */
}

void *glhost_getprocaddressARB(const unsigned char *name)
{
    return OSMesaGetProcAddress((const char *)name);
}

int glhost_swapintervalSGI(int interval)
{
    // ignored
    return 0;
}

#endif
