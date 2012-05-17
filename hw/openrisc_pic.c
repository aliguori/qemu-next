/*
 * Generic  OPENRISC Programmable Interrupt Controller support.
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                          Feng Gao <gf91597@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include "hw.h"
#include "openrisc_cpudev.h"
#include "cpu.h"

/* Reset PIC */
void cpu_openrisc_pic_reset(CPUOPENRISCState *env)
{
    env->picmr = 0x00000000;
    env->picsr = 0x00000000;
}
