/*
 * OpenRISC system-insns helper routines
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
#include "helper.h"

#define TO_SPR(group, number) (((group)<<11)+(number))
void HELPER(mtspr)(CPUOPENRISCState * env,
                   target_ulong ra, target_ulong rb, uint32_t offset)
{
#if !defined(CONFIG_USER_ONLY)
    int spr = env->gpr[ra] | offset;
    int idx;

    switch (spr) {
    case TO_SPR(0, 16): /* NPC */
        env->npc = env->gpr[rb];
        break;

    case TO_SPR(0, 17): /* SR */
        if ((env->sr & (SR_IME | SR_DME | SR_SM)) ^
            (env->gpr[rb] & (SR_IME | SR_DME | SR_SM))) {
            tlb_flush(env, 1);
        }
        env->sr = env->gpr[rb];
        env->sr |= SR_FO;      /* FO is const equal to 1 */
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
        break;

    case TO_SPR(0, 18): /* PPC */
        env->ppc = env->gpr[rb];
        break;

    case TO_SPR(0, 32): /* EPCR */
        env->epcr = env->gpr[rb];
        break;

    case TO_SPR(0, 48): /* EEAR */
        env->eear = env->gpr[rb];
        break;

    case TO_SPR(0, 64): /* ESR */
        env->esr = env->gpr[rb];
        break;
    case TO_SPR(1, 512) ... TO_SPR(1, 639): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        if (!(env->gpr[rb] & 1)) {
            tlb_flush_page(env, env->dtlb[0][idx].mr & TARGET_PAGE_MASK);
        }
        env->dtlb[0][idx].mr = env->gpr[rb];
        break;

    case TO_SPR(1, 640) ... TO_SPR(1, 767): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        env->dtlb[0][idx].tr = env->gpr[rb];
        break;
    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;
    case TO_SPR(2, 512) ... TO_SPR(2, 639):   /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        if (!(env->gpr[rb] & 1)) {
            tlb_flush_page(env, env->itlb[0][idx].mr & TARGET_PAGE_MASK);
        }
        env->itlb[0][idx].mr = env->gpr[rb];
        break;

    case TO_SPR(2, 640) ... TO_SPR(2, 767): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        env->itlb[0][idx].tr = env->gpr[rb];
    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;
    case TO_SPR(9, 0):  /* PICMR */
        cpu_openrisc_store_picmr(env, env->gpr[rb]);
        break;
    case TO_SPR(9, 2):  /* PICSR */
        cpu_openrisc_store_picsr(env, env->gpr[rb]);
        break;
    case TO_SPR(10, 0): /* TTMR */
        cpu_openrisc_store_compare(env, env->gpr[rb]);
        break;
    case TO_SPR(10, 1): /* TTCR */
        cpu_openrisc_store_count(env, env->gpr[rb]);
        break;
    default:
        break;
    }
#endif
}

void HELPER(mfspr)(CPUOPENRISCState * env,
                   target_ulong rd, target_ulong ra, uint32_t offset)
{
#if !defined(CONFIG_USER_ONLY)
    int spr = env->gpr[ra] | offset;
    int idx;

    switch (spr) {
    case TO_SPR(0, 0): /* VR */
        env->gpr[rd] = SPR_VR;
        break;

    case TO_SPR(0, 1): /* UPR */
        env->gpr[rd] = 0x619;    /* TT, DM, IM, UP present */
        break;

    case TO_SPR(0, 2): /* CPUCFGR */
        env->gpr[rd] = 0x000000a0;
        break;

    case TO_SPR(0, 3): /* DMMUCFGR */
        env->gpr[rd] = 0x18;    /* 1Way, 64 entries */
        break;
    case TO_SPR(0, 4): /* IMMUCFGR */
        env->gpr[rd] = 0x18;
        break;

    case TO_SPR(0, 16): /* NPC */
        env->gpr[rd] = env->npc;
        break;

    case TO_SPR(0, 17): /* SR */
        env->gpr[rd] = env->sr;
        break;

    case TO_SPR(0, 18): /* PPC */
        env->gpr[rd] = env->ppc;
        break;

    case TO_SPR(0, 32): /* EPCR */
        env->gpr[rd] = env->epcr;
        break;

    case TO_SPR(0, 48): /* EEAR */
        env->gpr[rd] = env->eear;
        break;

    case TO_SPR(0, 64): /* ESR */
        env->gpr[rd] = env->esr;
        break;
    case TO_SPR(1, 512) ... TO_SPR(1, 639): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        env->gpr[rd] = env->dtlb[0][idx].mr;
        break;

    case TO_SPR(1, 640) ... TO_SPR(1, 767): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        env->gpr[rd] = env->dtlb[0][idx].tr;
        break;
    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 639): /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        env->gpr[rd] = env->itlb[0][idx].mr;
        break;

    case TO_SPR(2, 640) ... TO_SPR(2, 767): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        env->gpr[rd] = env->itlb[0][idx].tr;
        break;
    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;
    case TO_SPR(9, 0):  /* PICMR */
        env->gpr[rd] = env->picmr;
        break;
    case TO_SPR(9, 2):  /* PICSR */
        env->gpr[rd] = env->picsr;
        break;
    case TO_SPR(10, 0): /* TTMR */
        env->gpr[rd] = env->ttmr;
        break;
    case TO_SPR(10, 1): /* TTCR */
        env->gpr[rd] = cpu_openrisc_get_count(env);
        break;
    default:
        break;
    }
#endif
}
