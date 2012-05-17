/*
 *  Openrisc MMU.
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                          Zhizhou Zhang <etouzh@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "gdbstub.h"
#include "helper.h"
#include "host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

#if !defined(CONFIG_USER_ONLY)
target_phys_addr_t cpu_get_phys_page_debug(CPUOPENRISCState *env,
                                           target_ulong addr)
{
    return 0;
}
#endif

void openrisc_reset(CPUOPENRISCState *env)
{
    env->pc = 0x100;
    env->sr = SR_FO | SR_SM;
    env->exception_index = -1;
}
