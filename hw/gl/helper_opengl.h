/*
 * OpenGL virtual hw driver interface
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef HELPER_OPENGL_H__
#define HELPER_OPENGL_H__

#include "qemu-common.h"
#include "qemugl.h"

#ifdef CONFIG_OSMESA
#define USE_OSMESA
#define GLX_OSMESA_FORCE_32BPP
#endif

/* the following flags are only usable when I/O framebuffer is not used */
#ifndef QEMUGL_IO_FRAMEBUFFER
/* optimize host->guest opengl frame copy routine by assuming:
 * - host endianess == guest endianess
 * - single pixel data never crosses page boundary
 * - no invalid buffer accesses (i.e. no bounds checking)
 */
#define QEMUGL_OPTIMIZE_FRAMECOPY
#endif // QEMUGL_IO_FRAMEBUFFER

struct helper_opengl_s {
    uint32_t fid;
    uint32_t pid;
    uint32_t rsp;
    uint32_t iap;
    uint32_t ias;
    uint32_t result;
    uint32_t bufsize;
    uint32_t bufpixelsize;
    uint32_t bufwidth;
#if defined(USE_OSMESA) || defined(WIN32)
    uint32_t bufcol;
#endif
#ifndef QEMUGL_IO_FRAMEBUFFER
    uint32_t qemugl_bufbytesperline;
    uint32_t qemugl_buf;
    
    struct {
        uint32_t count;
        uint32_t addr;
        uint8_t *ptr;
        void *mapped_ptr;
        target_phys_addr_t mapped_len;
    } framecopy;
#endif
    void *buf;
    CPUState *env;
};

/* helper_opengl.c */
void *helper_opengl_init(CPUState *env);
#ifndef QEMUGL_IO_FRAMEBUFFER
void helper_opengl_copyframe(struct helper_opengl_s *);
#endif // QEMUGL_IO_FRAMEBUFFER


#endif
