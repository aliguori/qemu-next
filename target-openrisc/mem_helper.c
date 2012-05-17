/*
 * OpenRISC mmu helper routines
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                          Zhizhou Zhang <etouzh@gmail.com>
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

#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

void tlb_fill(CPUOPENRISCState *env1, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    TranslationBlock *tb;
    CPUOPENRISCState *saved_env;
    unsigned long pc;
    int ret;

    saved_env = env;
    env = env1;

    ret = cpu_openrisc_handle_mmu_fault(env, addr, is_write, mmu_idx);

    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we
                   have a virtual CPU fault */
                cpu_restore_state(tb, env, pc);
            }
        }
        /* Raise Exception... */
        cpu_loop_exit(env);
    }
    env = saved_env;
}
#endif
