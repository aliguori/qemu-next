#ifndef OPENGL_HOST_COCOA_M__
#define OPENGL_HOST_COCOA_M__

// following dummy typedefs needed to be able to include helper_opengl.h here
typedef void CPUState;
typedef unsigned long target_phys_addr_t;

#include "helper_opengl.h"
#ifndef USE_OSMESA

#include "opengl_host_cocoa.h"

//#define COCOA_GLX_DEBUG

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", \
                                  __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef COCOA_GLX_DEBUG
#define GLXTRACE(fmt, ...) GL_ERROR(fmt, ##__VA_ARGS__)
#else
#define GLXTRACE(...)
#endif

static struct glx_visual_s {
    int visual_id;
    NSOpenGLPixelFormat *pf;
    struct glx_visual_s *next;
} *glx_visuals = 0;

static NSOpenGLPixelFormat *glhost_visuals_add(NSOpenGLPixelFormat *pf)
{
    static int next_free_visual_id = 1;
    
    struct glx_visual_s *p;
    if (!glx_visuals) {
        glx_visuals = malloc(sizeof(struct glx_visual_s));
        p = glx_visuals;
    } else {
        for (p = glx_visuals; p->next; p = p->next) {
            if (p->pf == pf) {
                return pf;
            } else if ([pf isEqual:p->pf] == YES) {
                [p->pf release];
                p->pf = pf;
                return pf;
            }
        }
        p->next = malloc(sizeof(struct glx_visual_s));
        p = p->next;
    }
    GLXTRACE("adding new visual with id 0x%x", next_free_visual_id);
    p->visual_id = next_free_visual_id++;
    p->pf = pf;
    p->next = 0;
    
    return pf;
}

GLHostVisualInfo glhost_getvisualinfo(int vis_id)
{
    GLXTRACE("vis_id = %d", vis_id);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->visual_id == vis_id)
            return p->pf;
    }
    return 0;
}

GLHostVisualInfo glhost_getdefaultvisual(void)
{
    GLXTRACE("returning default pixel format object");
    return glhost_choosevisual(0, NULL);
}

int glhost_idforvisual(GLHostVisualInfo vis)
{
    GLXTRACE("vis=%p", vis);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->pf == vis)
            return p->visual_id;
    }
    GL_ERROR("oops - unknown visual %p", vis);
    exit(-1);
    return 0;
}

GLHostVisualInfo glhost_choosevisual(int depth, const int *attribs)
{
    GLXTRACE("creating pixel format object... depth=%d bits/pixel", depth);
#ifdef QEMUGL_MULTITHREADED
    /* since we come here from a thread different than the qemu main
     * thread, we need to setup an autorelease pool ourself
     */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#endif
    unsigned int ns_attribs[] = {
        NSOpenGLPFAOffScreen,
        NSOpenGLPFAColorSize, depth,
        NSOpenGLPFAAlphaSize, 0,
        NSOpenGLPFADepthSize, 16,
        0
    };
    NSOpenGLPixelFormat *pixelformat = 0;
    pixelformat = glhost_visuals_add([[NSOpenGLPixelFormat alloc]
                                      initWithAttributes:ns_attribs]);
    GLint color_size = 0;
    [pixelformat getValues:&color_size forAttribute:NSOpenGLPFAColorSize
          forVirtualScreen:0];
#ifdef QEMUGL_MULTITHREADED
    [pool drain];
#endif
    GLXTRACE("created pixel format object %p, color size = %d",
             pixelformat, color_size);
    return pixelformat;
}

typedef struct {
    int width;
    int height;
    int depth;
    NSOpenGLContext *context;
    uint8_t *base;
} CocoaDrawable;

GLHostDrawable glhost_createdrawable(GLHostVisualInfo vis, int width,
                                     int height, int depth)
{
    depth = (depth + 7) / 8;
    GLXTRACE("creating drawable... %dx%d, depth %d bytes/pixel",
             width, height, depth);
    CocoaDrawable *drawable = qemu_mallocz(sizeof(CocoaDrawable));
    drawable->width = width;
    drawable->height = height;
    drawable->depth = depth;
    drawable->base = qemu_mallocz(width * height * depth);
    GLXTRACE("drawable %p created", drawable);
    return drawable;
}

void glhost_destroydrawable(GLHostDrawable drawable)
{
    GLXTRACE("releasing drawable %p", drawable);
    CocoaDrawable *d = (CocoaDrawable *)drawable;
    if (d) {
        qemu_free(d->base);
        d->base = 0;
        qemu_free(d);
    }
}

void glhost_resizedrawable(GLHostDrawable drawable, int width, int height)
{
    GLXTRACE("drawable %p, new size %dx%d", drawable, width, height);
    CocoaDrawable *d = (CocoaDrawable *)drawable;
    if (d && (d->width != width || d->height != height)) {
        NSOpenGLContext *context = [NSOpenGLContext currentContext];
        [context clearDrawable];
        d->base = qemu_realloc(d->base, width * height * d->depth);
        d->width = width;
        d->height = height;
        [context setOffScreen:d->base width:d->width height:d->height
                     rowbytes:d->width * d->depth];
    }
}

void *glhost_swapbuffers(GLHostDrawable drawable)
{
    //GLXTRACE("drawable = %p", drawable);
    CocoaDrawable *drw = (CocoaDrawable *)drawable;
    if (drw) {
        if (drw->context) {
            [drw->context flushBuffer];
        }
        return drw->base;
    }
    return NULL;
}

GLHostContext glhost_createcontext(GLHostVisualInfo vis, GLHostContext share)
{
    GLXTRACE("vis=%p, share=%p", vis, share);
    return [[NSOpenGLContext alloc]
            initWithFormat:(NSOpenGLPixelFormat *)vis
            shareContext:(NSOpenGLContext *)share];
}

void glhost_copycontext(GLHostContext src, GLHostContext dst,
                        unsigned long mask)
{
    GLXTRACE("src=%p, dst=%p, mask=0x%08x", src, dst, (unsigned int)mask);
    NSOpenGLContext *s = (NSOpenGLContext *)src;
    NSOpenGLContext *d = (NSOpenGLContext *)dst;
    [d copyAttributesFromContext:s withMask:mask];
}

int glhost_makecurrent(GLHostDrawable drawable, GLHostContext context)
{
    GLXTRACE("drawable=%p, context=%p", drawable, context);
    NSOpenGLContext *ctx = (NSOpenGLContext *)context;
    CocoaDrawable *drw = (CocoaDrawable *)drawable;
    if (drw) {
        drw->context = ctx;
        [ctx setOffScreen:drw->base width:drw->width height:drw->height
                 rowbytes:drw->width * drw->depth];
    } else {
        [ctx clearDrawable];
    }
    [ctx makeCurrentContext];
    return 1; /* always a success */
}

void glhost_destroycontext(GLHostContext context)
{
    GLXTRACE("context=%p", context);
    NSOpenGLContext *ctx = (NSOpenGLContext *)context;
    [ctx clearDrawable];
    [ctx release];
}

void glhost_destroypbuffer(GLHostPbuffer pbuffer)
{
    GLXTRACE("pbuffer=%p", pbuffer);
    NSOpenGLPixelBuffer *pbuf = (NSOpenGLPixelBuffer *)pbuffer;
    [pbuf release];
}

void glhost_xwaitgl(void)
{
    glFinish();
}

int glhost_getconfig(GLHostVisualInfo vis, int attrib, int *value)
{
    GLXTRACE("vis = %p, attrib = %d, value = %p", vis, attrib, value);
    NSOpenGLPixelFormat *pixelformat = (NSOpenGLPixelFormat *)vis;
    switch (attrib) {
        case 1: /* GLX_USE_GL */
            *value = 1;
            break;
        case 2: /* GLX_BUFFER_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAColorSize
                  forVirtualScreen:0];
            break;
        case 3: /* GLX_LEVEL */
            *value = 0;
            break;
        case 4: /* GLX_RGBA */
            *value = 1;
            break;
        case 5: /* GLX_DOUBLEBUFFER */
            [pixelformat getValues:value forAttribute:NSOpenGLPFADoubleBuffer
                  forVirtualScreen:0];
            break;
        case 6: /* GLX_STEREO */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAStereo
                  forVirtualScreen:0];
            break;
        case 7: /* GLX_AUX_BUFFERS */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAAuxBuffers
                  forVirtualScreen:0];
            break;
        case 8: /* GLX_RED_SIZE */
        case 9: /* GLX_GREEN_SIZE */
        case 10: /* GLX_BLUE_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAColorSize
                  forVirtualScreen:0];
            *value /= 4; /* rough estimate */
            break;
        case 11: /* GLX_ALPHA_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAAlphaSize
                  forVirtualScreen:0];
            break;
        case 12: /* GLX_DEPTH_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFADepthSize
                  forVirtualScreen:0];
            break;
        case 13: /* GLX_STENCIL_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAStencilSize
                  forVirtualScreen:0];
            break;
        case 14: /* GLX_ACCUM_RED_SIZE */
        case 15: /* GLX_ACCUM_GREEN_SIZE */
        case 16: /* GLX_ACCUM_BLUE_SIZE */
        case 17: /* GLX_ACCUM_ALPHA_SIZE */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAAccumSize
                  forVirtualScreen:0];
            *value /= 4; /* rough estimate */
            break;
        case 32: /* GLX_CONFIG_CAVEAT */
            *value = 0x8000; /* GLX_NONE */
            break;
        case 34: /* GLX_X_VISUAL_TYPE */
            *value = 0x8002; /* GLX_TRUE_COLOR */
            break;
        case 35: /* GLX_TRANSPARENT_TYPE */
            *value = 0x8000; /* GLX_NONE */
            break;
        case 36: /* GLX_TRANSPARENT_INDEX_VALUE */
        case 37: /* GLX_TRANSPARENT_RED_VALUE */
        case 38: /* GLX_TRANSPARENT_GREEN_VALUE */
        case 39: /* GLX_TRANSPARENT_BLUE_VALUE */
        case 40: /* GLX_TRANSPARENT_ALPHA_VALUE */
            *value = 0;
            break;
        case 100000: /* GLX_SAMPLE_BUFFERS */
            [pixelformat getValues:value forAttribute:NSOpenGLPFASampleBuffers
                  forVirtualScreen:0];
            break;
        case 100001: /* GLX_SAMPLES */
            [pixelformat getValues:value forAttribute:NSOpenGLPFASamples
                  forVirtualScreen:0];
            break;
        case 0x20b0: /* GLX_FLOAT_COMPONENTS_NV */
            [pixelformat getValues:value forAttribute:NSOpenGLPFAColorFloat
                  forVirtualScreen:0];
            break;
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
        case 0x801b: /* GLX_PRESERVED_CONTENTS */
            *value = 0;
            break;
        case 0x8002: /* GLX_TRUE_COLOR */
        case 0x8012: /* GLX_X_RENDERABLE */
        case 0x8014: /* GLX_RGBA_TYPE */
            *value = 1;
            break;
        case 0x800b: /* GLX_VISUAL_ID */
            *value = glhost_idforvisual(pixelformat);
            break;
        case 0x8010: /* GLX_DRAWABLE_TYPE */
            *value = 0x5; /* GLX_WINDOW_BIT | GLX_PBUFFER_BIT */
            break;
        case 0x8011: /* GLX_RENDER_TYPE */
            *value = 0x1; /* GLX_RGBA_BIT */
            break;
        case 0x8016: /* GLX_MAX_PBUFFER_WIDTH */
        case 0x8017: /* GLX_MAX_PBUFFER_HEIGHT */
            *value = 2048;
            break;
        case 0x8018: /* GLX_MAX_PBUFFER_PIXELS */
            *value = 2048 * 2048;
            break;
        default:
            GL_ERROR("oops - unsupported attribute %d\n", attrib);
            return 2; /* GLX_BAD_ATTRIBUTE */
    }
    return 0; /* success */
}

int glhost_swapintervalSGI(int interval)
{
    GLXTRACE("interval=%d", interval);
    [[NSOpenGLContext currentContext] setValues:&interval
                                   forParameter:NSOpenGLCPSwapInterval];
    return 0;
}

#include <dlfcn.h>

void *glhost_getprocaddressARB(const unsigned char *name)
{
    return dlsym(RTLD_DEFAULT, (const char *)name);
}

#endif
#endif
