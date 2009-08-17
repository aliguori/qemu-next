#ifndef OPENGL_HOST_H__
#define OPENGL_HOST_H__

/* visual handling */
GLHostVisualInfo glhost_getvisualinfo(int vis_id);
GLHostVisualInfo glhost_getdefaultvisual(void);
int glhost_idforvisual(GLHostVisualInfo vis);
GLHostVisualInfo glhost_choosevisual(int depth, const int *attribs);

/* drawable handling */
GLHostDrawable glhost_createdrawable(GLHostVisualInfo vis, int width,
                                     int height, int depth);
void glhost_destroydrawable(GLHostDrawable drawable);
void glhost_resizedrawable(GLHostDrawable drawable, int width, int height);
void *glhost_swapbuffers(GLHostDrawable drawable);

/* context handling */
GLHostContext glhost_createcontext(GLHostVisualInfo vis, GLHostContext share);
void glhost_copycontext(GLHostContext src, GLHostContext dst,
                        unsigned long mask);
int glhost_makecurrent(GLHostDrawable drawable, GLHostContext context);
void glhost_destroycontext(GLHostContext context);
void glhost_destroypbuffer(GLHostPbuffer pbuffer);

/* misc helpers */
void glhost_xwaitgl(void);
int glhost_getconfig(GLHostVisualInfo vis, int attrib, int *value);

/* GLX_ARB_get_proc_address extension */
void *glhost_getprocaddressARB(const unsigned char *name);

/* GLX_SGI_swap_control extension */
int glhost_swapintervalSGI(int interval);

#endif
