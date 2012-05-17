/*
 *  Openrisc virtual CPU header.
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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

#ifndef CPU_OPENRISC_H
#define CPU_OPENRISC_H

#define TARGET_HAS_ICE 1

#define ELF_MACHINE EM_OPENRISC

#define CPUArchState struct CPUOPENRISCState

#define TARGET_LONG_BITS 32

#include "config.h"
#include "qemu-common.h"
#include "cpu-defs.h"
#include "softfloat.h"

struct CPUOPENRISCState;

#define NB_MMU_MODES 3
#define MMU_NOMMU_IDX   0
#define MMU_SUPERVISOR_IDX  1
#define MMU_USER_IDX    2

#define TARGET_PAGE_BITS 13


#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define SET_FP_CAUSE(reg, v)    do {\
                                    (reg) = ((reg) & ~(0x3f << 12)) | \
                                            ((v & 0x3f) << 12);\
                                } while (0)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define UPDATE_FP_FLAGS(reg, v)   do {\
                                      (reg) |= ((v & 0x1f) << 2);\
                                  } while (0)

/* Internel flags, delay slot flag */
#define D_FLAG    1

#define NR_IRQS  32
#define PIC_MASK 0xFFFFFFFF

/* Verison Register */
#define SPR_VR       0x12000001
#define SPR_CPUCFGR  0x12000001

/* Registers */
enum {
    R0 = 0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10,
    R11, R12, R13, R14, R15, R16, R17, R18, R19, R20,
    R21, R22, R23, R24, R25, R26, R27, R28, R29, R30,
    R31
};

/* Register aliases */
enum {
    R_ZERO = R0,
    R_SP = R1,
    R_FP = R2,
    R_LR = R9,
    R_RV = R11,
    R_RVH = R12
};

/* Exceptions indices */
enum {
    EXCP_RESET    = 0x1,
    EXCP_BUSERR   = 0x2,
    EXCP_DPF      = 0x3,
    EXCP_IPF      = 0x4,
    EXCP_TICK     = 0x5,
    EXCP_ALIGN    = 0x6,
    EXCP_ILLEGAL  = 0x7,
    EXCP_INT      = 0x8,
    EXCP_DTLBMISS = 0x9,
    EXCP_ITLBMISS = 0xa,
    EXCP_RANGE    = 0xb,
    EXCP_SYSCALL  = 0xc,
    EXCP_FPE      = 0xd,
    EXCP_TRAP     = 0xe,
    EXCP_NR,
};

/* Supervisor register */
enum {
    SR_SM = 1,
    SR_TEE = (1<<1),
    SR_IEE = (1<<2),
    SR_DCE = (1<<3),
    SR_ICE = (1<<4),
    SR_DME = (1<<5),
    SR_IME = (1<<6),
    SR_LEE = (1<<7),
    SR_CE  = (1<<8),
    SR_F   = (1<<9),
    SR_CY  = (1<<10),
    SR_OV  = (1<<11),
    SR_OVE = (1<<12),
    SR_DSX = (1<<13),
    SR_EPH = (1<<14),
    SR_FO  = (1<<15),
    SR_SUMRA = (1<<16),
    SR_SCE = (1<<17),
};

/* FPCSR register */
enum {
    FPCSR_FPEE = 1,
    FPCSR_RM = (3 << 1),
    FPCSR_OVF = (1 << 3),
    FPCSR_UNF = (1 << 4),
    FPCSR_SNF = (1 << 5),
    FPCSR_QNF = (1 << 6),
    FPCSR_ZF = (1 << 7),
    FPCSR_IXF = (1 << 8),
    FPCSR_IVF = (1 << 9),
    FPCSR_INF = (1 << 10),
    FPCSR_DZF = (1 << 11),
};

/* TTMR bit */
enum {
    TTMR_TP = (0xfffffff),
    TTMR_IP = (1<<28),
    TTMR_IE = (1<<29),
    TTMR_M  = (3<<30),
};

enum {
    DTLB_WAYS = 1,
    DTLB_SIZE = 64,
    DTLB_MASK = (DTLB_SIZE-1),
    ITLB_WAYS = 1,
    ITLB_SIZE = 64,
    ITLB_MASK = (ITLB_SIZE-1),
};

/* TLB prot */
enum {
    URE = (1<<6),
    UWE = (1<<7),
    SRE = (1<<8),
    SWE = (1<<9),

    SXE = (1<<6),
    UXE = (1<<7),
};

typedef struct tlb_entry {
    uint32_t mr;
    uint32_t tr;
} tlb_entry;

typedef struct CPUOPENRISCState CPUOPENRISCState;
struct CPUOPENRISCState {
    target_ulong gpr[32];   /* General registers */
    uint32_t sr;            /* Supervisor register */
    target_ulong machi;     /* Multiply register  MACHI */
    target_ulong maclo;     /* Multiply register  MACLO */
    target_ulong fpmaddhi;  /* Multiply and add float register FPMADDHI */
    target_ulong fpmaddlo;  /* Multiply and add float register FPMADDLO */
    target_ulong epcr;      /* Exception PC register */
    target_ulong eear;      /* Exception EA register */
    uint32_t esr;           /* Exception supervisor register */
    void *irq[32];          /* Interrupt irq input */
    uint32_t fpcsr;         /* Float register */
    float_status fp_status;
    target_ulong pc;        /* Program counter */
    target_ulong npc;       /* Next PC */
    target_ulong ppc;       /* Prev PC */
    target_ulong jmp_pc;    /* Jump PC */
    uint32_t flags;

    /* Branch. */
    uint32_t btaken;        /* the SR_F bit */

#if !defined(CONFIG_USER_ONLY)
    /* MMU */
    tlb_entry(*itlb)[ITLB_SIZE], (*dtlb)[DTLB_SIZE];
    int (*map_address_code)(struct CPUOPENRISCState *env,
                            target_phys_addr_t *physical, int *prot,
                            target_ulong address, int rw);
    int (*map_address_data)(struct CPUOPENRISCState *env,
                            target_phys_addr_t *physical, int *prot,
                            target_ulong address, int rw);

    /* Internal Timer */
    struct QEMUTimer *timer;
    uint32_t ttmr;          /* Timer tick mode register */
    uint32_t ttcr;          /* Timer tick count register */

    uint32_t picmr;         /* Interrupt mask register */
    uint32_t picsr;         /* Interrupt contrl register*/
#endif

    CPU_COMMON
};

#include "cpu-qom.h"

void cpu_openrisc_list(FILE *f, fprintf_function cpu_fprintf);
CPUOPENRISCState *cpu_openrisc_init(const char *cpu_model);
int cpu_openrisc_exec(CPUOPENRISCState *s);
void do_interrupt(CPUOPENRISCState *env);
int cpu_openrisc_signal_handler(int host_signum, void *pinfo, void *puc);
void openrisc_translate_init(void);
int cpu_openrisc_handle_mmu_fault(CPUOPENRISCState *env, target_ulong address,
                                  int rw, int mmu_idx);
#define cpu_list cpu_openrisc_list
#define cpu_init cpu_openrisc_init
#define cpu_exec cpu_openrisc_exec
#define cpu_gen_code cpu_openrisc_gen_code
#define cpu_signal_handler cpu_openrisc_signal_handler
#define cpu_handle_mmu_fault cpu_openrisc_handle_mmu_fault

void openrisc_reset(CPUOPENRISCState *env);
#if !defined(CONFIG_USER_ONLY)
void cpu_openrisc_store_count(CPUOPENRISCState *env, target_ulong count);
void cpu_openrisc_store_compare(CPUOPENRISCState *env, target_ulong value);
uint32_t cpu_openrisc_get_count(CPUOPENRISCState *env);

void cpu_openrisc_pic_reset(CPUOPENRISCState *env);
void cpu_openrisc_store_picsr(CPUOPENRISCState *env, uint32_t value);
void cpu_openrisc_store_picmr(CPUOPENRISCState *env, uint32_t value);

void openrisc_mmu_init(CPUOPENRISCState *env);
int get_phys_nommu(CPUOPENRISCState *env, target_phys_addr_t *physical,
                   int *prot, target_ulong address, int rw);
int get_phys_code(CPUOPENRISCState *env, target_phys_addr_t *physical,
                  int *prot, target_ulong address, int rw);
int get_phys_data(CPUOPENRISCState *env, target_phys_addr_t *physical,
                  int *prot, target_ulong address, int rw);
#endif

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUOPENRISCState *env, target_ulong newsp)
{
    if (newsp) {
        env->gpr[1] = newsp;
    }
    env->gpr[2] = 0;
}
#endif

#include "cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUOPENRISCState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = env->flags;
}

static inline int cpu_mmu_index(CPUOPENRISCState *env)
{
    if (!(env->sr & SR_IME)) {
        return MMU_NOMMU_IDX;
    }
    return (env->sr & SR_SM) == 0 ? MMU_USER_IDX : MMU_SUPERVISOR_IDX;
}

#define CPU_INTERRUPT_TIMER   CPU_INTERRUPT_TGT_INT_0
static inline bool cpu_has_work(CPUOPENRISCState *env)
{
    return env->interrupt_request & (CPU_INTERRUPT_HARD |
                                     CPU_INTERRUPT_TIMER);
}

#include "exec-all.h"

static inline void cpu_pc_from_tb(CPUOPENRISCState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

#endif /* CPU_OPENRISC_H */
