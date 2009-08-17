#ifndef OPENGL_HOST_X11_H__
#define OPENGL_HOST_X11_H__

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>
#include "mesa_glx.h"

typedef GLXFBConfig GLHostVisualInfo;
typedef GLXContext GLHostContext;
typedef struct GLHostDrawable_s {
    int width, height, depth;
    GLXFBConfig visual;
    Pixmap pix;
    GLXPixmap glx_pix;
} *GLHostDrawable;
typedef GLXPbuffer GLHostPbuffer;

#include "opengl_host.h"

#define GLX_EXCESS_DEBUG

#define GL_ERROR(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", \
                                  __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef GLX_EXCESS_DEBUG
#define GLX_TRACE(...) GL_ERROR(__VA_ARGS__)
#else
#define GLX_TRACE(...)
#endif

static Display *x11_display(void)
{
    static Display *default_display = NULL;
    if (!default_display) {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        SDL_GetWMInfo(&info);
        default_display = info.info.x11.display;
    }
    return default_display;
}

static struct glx_visual_s {
    int id;
    GLXFBConfig *cfg;
    struct glx_visual_s *next;
} *glx_visuals = 0;

static GLXFBConfig glx_visuals_add(GLXFBConfig *cfg)
{
    int id = 0;
    int result = glXGetFBConfigAttrib(x11_display(), cfg[0],
                                      GLX_FBCONFIG_ID, &id);
    if (result != Success) {
        GL_ERROR("glXGetFBConfigAttrib failed: %d", result);
        return 0;
    }
    GLX_TRACE("cfg %p, id 0x%x", cfg[0], id);
    struct glx_visual_s *p;
    if (!glx_visuals) {
        glx_visuals = malloc(sizeof(struct glx_visual_s));
        p = glx_visuals;
    } else {
        for (p = glx_visuals; p->next; p = p->next) {
            if (p->cfg[0] == cfg[0] || p->id == id) {
                GLX_TRACE("already cached as %p", p->cfg[0]);
                XFree(cfg);
                return p->cfg[0];
            }
        }
        p->next = malloc(sizeof(struct glx_visual_s));
        p = p->next;
    }
    GLX_TRACE("adding new visual %p with id 0x%x", cfg[0], id);
    p->id = id;
    p->cfg = cfg;
    p->next = 0;
    
    return p->cfg[0];
}

GLHostVisualInfo glhost_getvisualinfo(int vis_id)
{
    GLX_TRACE("vis_id = 0x%x", vis_id);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->id == vis_id) {
            return p->cfg[0];
        }
    }
    GL_ERROR("unknown visual id 0x%x", vis_id);
    return 0;
}

static GLHostVisualInfo x11_getgoodvisualfordepth(int depth)
{
    GLX_TRACE("depth=%d", depth);
    Display *dpy = x11_display();
    int attribs[] = {
        GLX_RED_SIZE, depth / 3,
        GLX_GREEN_SIZE, depth / 3,
        GLX_BLUE_SIZE, depth / 3,
        GLX_ALPHA_SIZE, 0,
        GLX_DEPTH_SIZE, 1,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT | GLX_WINDOW_BIT,
        GLX_DOUBLEBUFFER, True,
        GLX_CONFIG_CAVEAT, GLX_NONE,
        None
    };
    int count = 0;
    GLXFBConfig *cfg = glXChooseFBConfig(dpy, DefaultScreen(dpy),
                                         attribs, &count);
    GLX_TRACE("%d visual candidates found...", count);
    if (count > 0 && cfg) {
        int match_id = -1, best_alpha = 255;
        int i = 0;
        for (; i < count; i++) {
            int red = 0, green = 0, blue = 0, alpha = 0;
            glXGetFBConfigAttrib(dpy, cfg[i], GLX_RED_SIZE, &red);
            glXGetFBConfigAttrib(dpy, cfg[i], GLX_GREEN_SIZE, &green);
            glXGetFBConfigAttrib(dpy, cfg[i], GLX_BLUE_SIZE, &blue);
            glXGetFBConfigAttrib(dpy, cfg[i], GLX_ALPHA_SIZE, &alpha);
            GLX_TRACE("candidate #%d: %d-%d-%d-%d", i + 1,
                      red, green, blue, alpha);
            if (red + green + blue + alpha <= depth &&
                alpha < best_alpha) {
                best_alpha = alpha;
                int result = glXGetFBConfigAttrib(dpy, cfg[i],
                                                  GLX_FBCONFIG_ID, &match_id);
                if (result != Success) {
                    GL_ERROR("glXGetFBConfigAttrib failed: %d", result);
                } else {
                    GLX_TRACE("this is the best so far, it's id is 0x%x",
                              match_id);
                }
            }
        }
        XFree(cfg);
        if (match_id != -1) {
            int attr[] = {GLX_FBCONFIG_ID, match_id};
            cfg = glXChooseFBConfig(dpy, DefaultScreen(dpy), attr, &count);
            return glx_visuals_add(cfg);
        }
    }
    return 0;
}

GLHostVisualInfo glhost_getdefaultvisual(void)
{
    GLX_TRACE("searching for default visual...");
    Display *dpy = x11_display();
    GLHostVisualInfo vis = x11_getgoodvisualfordepth(
        DefaultDepth(dpy, DefaultScreen(dpy)));
    if (!vis) {
        GL_ERROR("no good default visual found");
    }
    return vis;
}

int glhost_idforvisual(GLHostVisualInfo vis)
{
    GLX_TRACE("vis = %p", vis);
    struct glx_visual_s *p = glx_visuals;
    for (; p; p = p->next) {
        if (p->cfg[0] == vis) {
            return p->id;
        }
    }
    GL_ERROR("unknown visual %p", vis);
    return 0;
}

GLHostVisualInfo glhost_choosevisual(int depth, const int *attribs)
{
    GLX_TRACE("depth=%d",depth);
    GLHostVisualInfo vis = x11_getgoodvisualfordepth(depth);
    if (!vis) {
        GL_ERROR("no good visual found for depth %d", depth);
    }
    return vis;
}

static void glx_createdrawable_buffer(GLHostDrawable d)
{
    GLX_TRACE("%dx%d, depth %d, visual %p", d->width, d->height, d->depth,
              d->visual);
    Display *dpy = x11_display();
    Window root = DefaultRootWindow(dpy);
    GLX_TRACE("create pixmap...");
    d->pix = XCreatePixmap(dpy, root, d->width, d->height, d->depth);
    if (!d->pix) {
        GL_ERROR("XCreatePixmap failed");
    }
    int attribs[] = {None};
    GLX_TRACE("create glxpixmap...");
    d->glx_pix = glXCreatePixmap(dpy, d->visual, d->pix, attribs);
    if (!d->glx_pix) {
        GL_ERROR("glXCreatePixmap failed");
    }
}

static void glx_destroydrawable_buffer(GLHostDrawable d)
{
    Display *dpy = x11_display();
    glXDestroyPixmap(dpy, d->glx_pix);
    XFreePixmap(dpy, d->pix);
}

GLHostDrawable glhost_createdrawable(GLHostVisualInfo vis, int width,
                                     int height, int depth)
{
    GLX_TRACE("vis %p, size %dx%d, depth %d", vis, width, height, depth);
    GLHostDrawable d = (GLHostDrawable)qemu_mallocz(sizeof(*d));
    d->width = width;
    d->height = height;
    d->depth = depth;
    d->visual = vis;
    glx_createdrawable_buffer(d);
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
    Display *dpy = x11_display();
    GLXContext context;
    int reassign = (glXGetCurrentDrawable() == drawable->glx_pix);
    if (reassign) {
        context = glXGetCurrentContext();
        glXMakeCurrent(dpy, None, NULL);
    }
    glx_destroydrawable_buffer(drawable);
    drawable->width = width;
    drawable->height = height;
    glx_createdrawable_buffer(drawable);
    if (reassign) {
        glXMakeCurrent(dpy, drawable->glx_pix, context);
    }
}

void *glhost_swapbuffers(GLHostDrawable drawable)
{
    Display *dpy = x11_display();
    glXSwapBuffers(dpy, drawable->glx_pix);
    XImage *img = XGetImage(dpy, drawable->pix, 0, 0, drawable->width,
                            drawable->height, 0xffffffff, ZPixmap);
    if (img) {
        return img->data;
    }
    return 0;
}

GLHostContext glhost_createcontext(GLHostVisualInfo vis, GLHostContext share)
{
    GLX_TRACE("vis=%p, share=%p", vis, share);
    return glXCreateNewContext(x11_display(), vis, GLX_RGBA_TYPE, share,
                               True);
}

void glhost_copycontext(GLHostContext src, GLHostContext dst,
                        unsigned long mask)
{
    GLX_TRACE("src=%p, dst=%p, mask=0x%lx", src, dst, mask);
    glXCopyContext(x11_display(), src, dst, mask);
}

int glhost_makecurrent(GLHostDrawable drawable, GLHostContext context)
{
    GLX_TRACE("drawable=%p, context %p", drawable, context);
    return glXMakeCurrent(x11_display(), drawable->glx_pix, context);
}

void glhost_destroycontext(GLHostContext context)
{
    GLX_TRACE("context %p", context);
    glXDestroyContext(x11_display(), context);
}

void glhost_destroypbuffer(GLHostPbuffer pbuffer)
{
    GLX_TRACE("pbuffer 0x%x", (int)pbuffer);
    glXDestroyPbuffer(x11_display(), pbuffer);
}

void glhost_xwaitgl(void)
{
    glXWaitGL();
}

int glhost_getconfig(GLHostVisualInfo vis, int attrib, int *value)
{
    return glXGetFBConfigAttrib(x11_display(), vis, attrib, value);
}

void *glhost_getprocaddressARB(const unsigned char *name)
{
    return glXGetProcAddressARB(name);
}

int glhost_swapintervalSGI(int interval)
{
    static int (*fptr)(int) = 0;
    static int queried = 0;
    if (!fptr && !queried) {
        queried = 1;
        fptr = (int (*)(int))glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalSGI");
    }
    if (fptr) {
        return fptr(interval);
    }
    return 0; // by default pretend it's ok
}

#endif
