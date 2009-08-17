/*
 *  Hand-implemented GL/GLX API
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

MAGIC_MACRO(_init),
MAGIC_MACRO(_exit_process),
MAGIC_MACRO(_createDrawable),
MAGIC_MACRO(_resizeDrawable),

MAGIC_MACRO(glGetString),

/* When you add a glX call here, you HAVE TO update IS_GLX_CALL */
MAGIC_MACRO(glXChooseVisual),
//GIC_MACRO(glXQueryExtensionsString),
//GIC_MACRO(glXQueryServerString),
MAGIC_MACRO(glXCreateContext),
MAGIC_MACRO(glXCopyContext),
MAGIC_MACRO(glXDestroyContext),
//GIC_MACRO(glXGetClientString),
//GIC_MACRO(glXQueryVersion),
MAGIC_MACRO(glXMakeCurrent),
MAGIC_MACRO(glXGetConfig),
MAGIC_MACRO(glXGetConfig_extended),
MAGIC_MACRO(glXWaitGL),
//GIC_MACRO(glXWaitX),
//GIC_MACRO(glXGetFBConfigAttrib_extended),
//GIC_MACRO(glXChooseFBConfig),
//GIC_MACRO(glXChooseFBConfigSGIX),
//GIC_MACRO(glXGetFBConfigs),
//GIC_MACRO(glXCreatePbuffer),
//GIC_MACRO(glXCreateGLXPbufferSGIX),
MAGIC_MACRO(glXDestroyPbuffer),
//GIC_MACRO(glXDestroyGLXPbufferSGIX),
//GIC_MACRO(glXCreateNewContext),
//GIC_MACRO(glXCreateContextWithConfigSGIX),
//GIC_MACRO(glXGetVisualFromFBConfig),
//GIC_MACRO(glXGetFBConfigAttrib),
//GIC_MACRO(glXGetFBConfigAttribSGIX),
//GIC_MACRO(glXQueryContext),
//GIC_MACRO(glXQueryDrawable),
//GIC_MACRO(glXQueryGLXPbufferSGIX),
//GIC_MACRO(glXUseXFont),
//GIC_MACRO(glXIsDirect),
MAGIC_MACRO(glXGetProcAddress_fake),
MAGIC_MACRO(glXGetProcAddress_global_fake),
MAGIC_MACRO(glXSwapBuffers),
//GIC_MACRO(glXQueryExtension),
//GIC_MACRO(glXGetScreenDriver),
//GIC_MACRO(glXGetDriverConfig),
MAGIC_MACRO(glXSwapIntervalSGI),
//GIC_MACRO(glXBindTexImageATI),
//GIC_MACRO(glXReleaseTexImageATI),
//GIC_MACRO(glXBindTexImageARB),
//GIC_MACRO(glXReleaseTexImageARB),
