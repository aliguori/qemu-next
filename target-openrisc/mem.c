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
enum {
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

tlb_entry itlb_table[ITLB_WAYS][ITLB_SIZE];
tlb_entry dtlb_table[DTLB_WAYS][DTLB_SIZE];
#endif

#if !defined(CONFIG_USER_ONLY)
/* no MMU emulation */
int get_phys_nommu(CPUOPENRISCState *env, target_phys_addr_t *physical,
                   int *prot, target_ulong address, int rw)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE;
    return TLBRET_MATCH;
}
int get_phys_code(CPUOPENRISCState *env, target_phys_addr_t *physical,
                  int *prot, target_ulong address, int rw)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & ITLB_MASK;
    int right = 0;

    if ((env->itlb[0][idx].mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(env->itlb[0][idx].mr & 1)) {
        return TLBRET_INVALID;
    }

    if (env->sr & SR_SM) { /* supervisor mode */
        if (env->itlb[0][idx].tr & SXE) {
            right |= PAGE_EXEC;
        }
    } else {
        if (env->itlb[0][idx].tr & UXE) {
            right |= PAGE_EXEC;
        }
    }

    if ((rw & 2) && ((right & PAGE_EXEC) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (env->itlb[0][idx].tr & TARGET_PAGE_MASK) |
                (address & (TARGET_PAGE_SIZE-1));
    *prot = right;
    return TLBRET_MATCH;
}

int get_phys_data(CPUOPENRISCState *env, target_phys_addr_t *physical,
                  int *prot, target_ulong address, int rw)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & DTLB_MASK;
    int right = 0;

    if ((env->dtlb[0][idx].mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(env->dtlb[0][idx].mr & 1)) {
        return TLBRET_INVALID;
    }

    if (env->sr & SR_SM) { /* supervisor mode */
        if (env->dtlb[0][idx].tr & SRE) {
            right |= PAGE_READ;
        }
        if (env->dtlb[0][idx].tr & SWE) {
            right |= PAGE_WRITE;
        }
    } else {
        if (env->dtlb[0][idx].tr & URE) {
            right |= PAGE_READ;
        }
        if (env->dtlb[0][idx].tr & UWE) {
            right |= PAGE_WRITE;
        }
    }

    if ((rw & 0) && ((right & PAGE_READ) == 0)) {
        return TLBRET_BADADDR;
    }
    if ((rw & 1) && ((right & PAGE_WRITE) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (env->dtlb[0][idx].tr & TARGET_PAGE_MASK) |
                (address & (TARGET_PAGE_SIZE-1));
    *prot = right;
    return TLBRET_MATCH;
}

static int get_physical_address(CPUOPENRISCState *env,
                                target_phys_addr_t *physical,
                                int *prot, target_ulong address,
                                int rw)
{
    int ret = TLBRET_MATCH;

    /* [0x0000--0x2000]: unmapped */
    if (address < 0x2000 && (env->sr & SR_SM)) {
        *physical = address;
        *prot = PAGE_READ | PAGE_WRITE;
        return ret;
    }

    if (rw == 2) { /* ITLB */
       *physical = 0;
        ret = env->map_address_code(env, physical,
                                    prot, address, rw);
    } else {       /* DTLB */
        ret = env->map_address_data(env, physical,
                                    prot, address, rw);
    }

    return ret;
}
#endif

static void raise_mmu_exception(CPUOPENRISCState *env, target_ulong address,
                                int rw, int tlb_error)
{
    int exception = 0;

    switch (tlb_error) {
    default:
        if (rw == 2) {
            exception = EXCP_IPF;
        } else {
            exception = EXCP_DPF;
        }
        break;
#if !defined(CONFIG_USER_ONLY)
    case TLBRET_BADADDR:
        if (rw == 2) {
            exception = EXCP_IPF;
        } else {
            exception = EXCP_DPF;
        }
        break;
    case TLBRET_INVALID:
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (rw == 2) {
            exception = EXCP_ITLBMISS;
        } else {
            exception = EXCP_DTLBMISS;
        }
        break;
#endif
    }

    env->exception_index = exception;
    env->eear = address;
}

#if !defined(CONFIG_USER_ONLY)
int cpu_openrisc_handle_mmu_fault(CPUOPENRISCState *env,
                                  target_ulong address, int rw, int mmu_idx)
{
    int ret = 0;
    target_phys_addr_t physical = 0;
    int prot = 0;

    ret = get_physical_address(env, &physical, &prot,
                               address, rw);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(env, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot | PAGE_EXEC,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
    } else if (ret < 0) {
        raise_mmu_exception(env, address, rw, ret);
        ret = 1;
    }

    return ret;
}
#else
int cpu_openrisc_handle_mmu_fault(CPUOPENRISCState *env,
                                  target_ulong address, int rw, int mmu_idx)
{
    int ret = 0;

    raise_mmu_exception(env, address, rw, ret);
    ret = 1;

    return ret;
}
#endif

#if !defined(CONFIG_USER_ONLY)
target_phys_addr_t cpu_get_phys_page_debug(CPUOPENRISCState *env,
                                           target_ulong addr)
{
    target_phys_addr_t phys_addr;
    int prot;

    if (get_physical_address(env, &phys_addr, &prot, addr, 0)) {
        return -1;
    }
    return phys_addr;
}

void openrisc_mmu_init(CPUOPENRISCState *env)
{
    env->map_address_code = &get_phys_nommu;
    env->map_address_data = &get_phys_nommu;
    env->dtlb = dtlb_table;
    env->itlb = itlb_table;
    memset(dtlb_table, 0, sizeof(tlb_entry)*DTLB_SIZE*DTLB_WAYS);
    memset(itlb_table, 0, sizeof(tlb_entry)*ITLB_SIZE*ITLB_WAYS);
}
#endif

void openrisc_reset(CPUOPENRISCState *env)
{
    env->pc = 0x100;
    env->sr = SR_FO | SR_SM;
    env->exception_index = -1;
#if !defined(CONFIG_USER_ONLY)
    openrisc_mmu_init(env);
    cpu_openrisc_pic_reset(env);
#endif
}
