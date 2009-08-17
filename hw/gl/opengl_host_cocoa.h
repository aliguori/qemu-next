#ifndef OPENGL_HOST_COCOA_H__
#define OPENGL_HOST_COCOA_H__

#define GLX_USE_GL 1
#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_STEREO 6

typedef void *GLHostContext;
typedef void *GLHostDrawable;
typedef void *GLHostPbuffer;
typedef void *GLHostVisualInfo;

#include "opengl_host.h"

#endif
