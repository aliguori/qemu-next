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

#ifdef CONFIG_OSMESA
#define USE_OSMESA
#define GLX_OSMESA_FORCE_32BPP
#endif

struct helper_opengl_s {
    uint32_t fid;
    uint32_t pid;
    uint32_t rsp;
    uint32_t iap;
    uint32_t ias;
    uint32_t result;
    uint32_t bufsize;
    uint32_t bufpixelsize;
#if defined(USE_OSMESA) || defined(WIN32)
    uint32_t bufwidth;
    uint32_t bufcol;
#endif
    void *buf;
    CPUState *env;
};

/* helper_opengl.c */
void *helper_opengl_init(CPUState *env, target_phys_addr_t base);

#endif
