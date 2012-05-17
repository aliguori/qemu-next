/*
 * Openrisc translation
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-common.h"
#include "qemu-log.h"
#include "config.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

#define DISAS_OPENRISC 1
#if DISAS_OPENRISC
#  define LOG_DIS(...) do { } while (0)
#endif

typedef struct DisasContext {
    CPUOPENRISCState *env;
    TranslationBlock *tb;
    target_ulong pc;
    target_ulong ppc, npc;
    uint32_t tb_flags;
    uint32_t is_jmp;
    uint32_t mem_idx;
    int singlestep_enabled;
    uint32_t delayed_branch;
} DisasContext;

void openrisc_translate_init(void)
{
}

static inline void gen_intermediate_code_internal(CPUOPENRISCState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
}

void gen_intermediate_code(CPUOPENRISCState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc(CPUOPENRISCState *env,
                              struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state(CPUOPENRISCState *env, FILE *f,
                    fprintf_function cpu_fprintf,
                    int flags)
{
    cpu_fprintf(f, "PC=%08x\n", env->pc);
}

void restore_state_to_opc(CPUOPENRISCState *env, TranslationBlock *tb,
                          int pc_pos)
{
    env->pc = gen_opc_pc[pc_pos];
}
