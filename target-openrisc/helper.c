/*
 * Openrisc helpers
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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
#include "qemu-common.h"
#include "gdbstub.h"
#include "helper.h"
#include "host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

typedef struct {
    const char *name;
} OPENRISCDef;

static const OPENRISCDef openrisc_defs[] = {
    {.name = "or1200",}
};

void cpu_openrisc_list(FILE *f, fprintf_function cpu_fprintf)
{
    int i;

    cpu_fprintf(f, "Available CPUs:\n");
    for (i = 0; i < ARRAY_SIZE(openrisc_defs); ++i) {
        cpu_fprintf(f, "  %s\n", openrisc_defs[i].name);
    }
}

CPUOPENRISCState *cpu_openrisc_init(const char *cpu_model)
{
    CPUOPENRISCState *env;
    static int tcg_inited;

    env = g_malloc0(sizeof(*env));
    memset(env, 0, sizeof(*env));
    cpu_exec_init(env);
    qemu_init_vcpu(env);
    if (!tcg_inited) {
        tcg_inited = 1;
        openrisc_translate_init();
    }

    return env;
}

void do_interrupt(CPUOPENRISCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    if (env->flags & D_FLAG) { /* Delay Slot insn */
        env->flags &= ~D_FLAG;
        env->sr |= SR_DSX;
        if (env->exception_index == EXCP_TICK    ||
            env->exception_index == EXCP_INT     ||
            env->exception_index == EXCP_SYSCALL ||
            env->exception_index == EXCP_FPE) {
            env->epcr = env->jmp_pc;
        } else {
            env->epcr = env->pc - 4;
        }
    } else {
        if (env->exception_index == EXCP_TICK    ||
            env->exception_index == EXCP_INT     ||
            env->exception_index == EXCP_SYSCALL ||
            env->exception_index == EXCP_FPE) {
            env->epcr = env->npc;
        } else {
            env->epcr = env->pc;
        }
    }

    tlb_flush(env, 1);

    env->esr = env->sr;
    env->sr &= ~SR_DME;
    env->sr &= ~SR_IME;
    env->sr |= SR_SM;
    env->sr &= ~SR_IEE;
    env->sr &= ~SR_TEE;
    env->map_address_data = &get_phys_nommu;
    env->map_address_code = &get_phys_nommu;

    if (env->exception_index > 0 && env->exception_index < EXCP_NR) {
        env->pc = env->exception_index * 0x100;
    } else {
        cpu_abort(env, "Unhandled exception 0x%x\n", env->exception_index);
    }
#endif

    env->exception_index = -1;
}
