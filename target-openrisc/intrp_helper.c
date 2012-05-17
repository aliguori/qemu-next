/*
 * OpenRISC interrupt helper routines
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

#include "cpu.h"
#include "helper.h"

void HELPER(rfe)(CPUOPENRISCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    int need_flush_tlb = (env->sr & (SR_SM | SR_IME | SR_DME)) ^
                         (env->esr & (SR_SM | SR_IME | SR_DME));
#endif
    env->pc = env->epcr;
    env->npc = env->epcr;
    env->sr = env->esr;

#if !defined(CONFIG_USER_ONLY)
    if (env->sr & SR_DME) {
        env->map_address_data = &get_phys_data;
    } else {
        env->map_address_data = &get_phys_nommu;
    }

    if (env->sr & SR_IME) {
        env->map_address_code = &get_phys_code;
    } else {
        env->map_address_code = &get_phys_nommu;
    }

    if (need_flush_tlb) {
        tlb_flush(env, 1);
    }
#endif
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}
