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
#ifndef OPENGL_EXEC_H__
#define OPENGL_EXEC_H__

#include "helper_opengl.h"

/* opengl_exec.c */
void init_process_tab(void);
int do_function_call(struct helper_opengl_s *s,
                     int func_number,
                     int pid,
                     target_phys_addr_t *args,
                     char *ret_string);

#endif
