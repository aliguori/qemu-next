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

static TCGv_ptr cpu_env;
static TCGv cpu_sr;
static TCGv cpu_R[32];
static TCGv cpu_pc;
static TCGv jmp_pc;        /* l.jr/l.jalr temp pc */
static TCGv cpu_npc;
static TCGv cpu_ppc;
static TCGv_i32 env_btaken;    /* bf/bnf , F flag taken */
static TCGv machi, maclo;
static TCGv fpmaddhi, fpmaddlo;
static TCGv_i32 env_flags;
#include "gen-icount.h"

void openrisc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    };
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_sr = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOPENRISCState, sr), "sr");
    env_flags = tcg_global_mem_new_i32(TCG_AREG0,
                                       offsetof(CPUOPENRISCState, flags),
                                       "flags");
    cpu_pc = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOPENRISCState, pc), "pc");
    cpu_npc = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUOPENRISCState, npc), "npc");
    cpu_ppc = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUOPENRISCState, ppc), "ppc");
    jmp_pc = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOPENRISCState, jmp_pc), "jmp_pc");
    env_btaken = tcg_global_mem_new_i32(TCG_AREG0,
                                        offsetof(CPUOPENRISCState, btaken),
                                        "btaken");
    machi = tcg_global_mem_new(TCG_AREG0,
                               offsetof(CPUOPENRISCState, machi),
                               "machi");
    maclo = tcg_global_mem_new(TCG_AREG0,
                               offsetof(CPUOPENRISCState, maclo),
                               "maclo");
    fpmaddhi = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUOPENRISCState, fpmaddhi),
                                  "fpmaddhi");
    fpmaddlo = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUOPENRISCState, fpmaddlo),
                                  "fpmaddlo");
    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new(TCG_AREG0,
                                      offsetof(CPUOPENRISCState, gpr[i]),
                                      regnames[i]);
    }
#define GEN_HELPER 2
#include "helper.h"
}

/* Writeback SR_F transaltion-space to execution-space.  */
static inline void wb_SR_F(void)
{
    int label;

    label = gen_new_label();
    tcg_gen_andi_tl(cpu_sr, cpu_sr, ~SR_F);
    tcg_gen_brcondi_tl(TCG_COND_EQ, env_btaken, 0, label);
    tcg_gen_ori_tl(cpu_sr, cpu_sr, SR_F);
    gen_set_label(label);
}

static inline int zero_extend(unsigned int val, int width)
{
    return val & ((1 << width) - 1);
}

static inline int sign_extend(unsigned int val, int width)
{
    int sval;

    /* LSL.  */
    val <<= 32 - width;
    sval = val;
    /* ASR.  */
    sval >>= 32 - width;
    return sval;
}

/* General purpose registers moves. */
static inline void gen_load_gpr(TCGv t, unsigned int reg)
{
    if (reg == 0) {
        tcg_gen_movi_tl(t, 0);
    } else {
        tcg_gen_mov_tl(t, cpu_R[reg]);
    }
}

static void gen_exception(DisasContext *dc, unsigned int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free(tmp);
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = dc->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) &&
                                       likely(!dc->singlestep_enabled)) {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_goto_tb(n);
        tcg_gen_exit_tb((tcg_target_long)tb + n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (dc->singlestep_enabled) {
            gen_exception(dc, EXCP_DEBUG);
        }
        tcg_gen_exit_tb(0);
    }
}

static inline uint32_t field(uint32_t val, int start, int length)
{
    val >>= start;
    val &= ~(~0 << length);
    return val;
}

static void dec_calc(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0, op1, op2;
    uint32_t ra, rb, rd;
    op0 = field(insn, 0, 4);
    op1 = field(insn, 8, 2);
    op2 = field(insn, 6, 2);
    ra = field(insn, 16, 5);
    rb = field(insn, 11, 5);
    rd = field(insn, 21, 5);

    switch (op0) {
    case 0x0000:
        switch (op1) {
        case 0x00:     /*l.add*/
            LOG_DIS("l.add r%d, r%d, r%d\n", rd, ra, rb);
            /* Aha, we do need a helper here for add */
            break;
        default:
            break;
        }
    break;

    case 0x0001:    /*l.addc*/
        switch (op1) {
        case 0x00:
            LOG_DIS("l.addc r%d, r%d, r%d\n", rd, ra, rb);
            /* Aha, we do need a helper here for addc */
            break;
        default:
            break;
        }
    break;

    case 0x0002:    /*l.sub*/
        switch (op1) {
        case 0x00:
            LOG_DIS("l.sub r%d, r%d, r%d\n", rd, ra, rb);
            /* Aha, we do need a helper here for sub */
            break;
        default:
            break;
        }
    break;

    case 0x0003:   /*l.and*/
        switch (op1) {
        case 0x00:
            LOG_DIS("l.and r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_and_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            break;
        }
    break;

    case 0x0004:   /*l.or*/
        switch (op1) {
        case 0x00:
            LOG_DIS("l.or r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_or_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            break;
        }
    break;

    case 0x0005:
        switch (op1) {
        case 0x00:   /*l.xor*/
            LOG_DIS("l.xor r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_xor_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            break;
        }
    break;

    case 0x0006:
        switch (op1) {
        case 0x03:   /*l.mul*/
            LOG_DIS("l.mul r%d, r%d, r%d\n", rd, ra, rb);
            if (ra != 0 && rb != 0) {
                tcg_gen_mul_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                tcg_gen_ext32s_tl(cpu_R[rd], cpu_R[rd]);
            } else {
                tcg_gen_movi_tl(cpu_R[rd], 0x0);
            }
        break;
        default:
            break;
        }
    break;

    case 0x0009:
        switch (op1) {
        case 0x03:   /*l.div*/
            LOG_DIS("l.div r%d, r%d, r%d\n", rd, ra, rb);
            if (rb != 0) {
                tcg_gen_div_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            } else {
                gen_exception(dc, EXCP_RANGE);
            }
            break;
        default:
            break;
        }
        break;

    case 0x000a:
        switch (op1) {
        case 0x03:   /*l.divu*/
            LOG_DIS("l.divu r%d, r%d, r%d\n", rd, ra, rb);
            if (rb != 0) {
                tcg_gen_divu_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            } else {
                gen_exception(dc, EXCP_RANGE);
            }
            break;
        default:
            break;
        }
        break;

    case 0x000b:
        switch (op1) {
        case 0x03:   /*l.mulu*/
            LOG_DIS("l.mulu r%d, r%d, r%d\n", rd, ra, rb);
            if (rb != 0 && ra != 0) {
                tcg_gen_ext32u_tl(cpu_R[ra], cpu_R[ra]);
                tcg_gen_ext32u_tl(cpu_R[rb], cpu_R[rb]);
                tcg_gen_mul_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            } else {
                tcg_gen_movi_tl(cpu_R[rd], 0);
            }
            break;
        default:
            break;
        }
        break;

    case 0x000e:
        switch (op1) {
        case 0x00:   /*l.cmov*/
            LOG_DIS("l.cmov r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab = gen_new_label();
                TCGv sr_f = tcg_temp_new();
                tcg_gen_andi_tl(sr_f, cpu_sr, SR_F);
                tcg_gen_mov_tl(cpu_R[rd], cpu_R[rb]);
                tcg_gen_brcondi_tl(TCG_COND_NE, sr_f, SR_F, lab);
                tcg_gen_mov_tl(cpu_R[rd], cpu_R[ra]);
                gen_set_label(lab);
                tcg_temp_free(sr_f);
            }
            break;
        default:
            break;
        }
        break;

    case 0x000f:
        switch (op1) {
        case 0x00:   /*l.ff1*/
            LOG_DIS("l.ff1 r%d, r%d, r%d\n", rd, ra, rb);
            /* ff1 need a helper here */
            break;
        case 0x01:   /*l.fl1*/
            LOG_DIS("l.fl1 r%d, r%d, r%d\n", rd, ra, rb);
            /* fl1 need a helper here */
            break;
        default:
            break;
        }
        break;

    case 0x0008:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:   /*l.sll*/
                LOG_DIS("l.sll r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shl_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x01:   /*l.srl*/
                LOG_DIS("l.srl r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x02:   /*l.sra*/
                LOG_DIS("l.sra r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_sar_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x03:   /*l.ror*/
                LOG_DIS("l.ror r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_rotr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        break;

    case 0x000c:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:   /*l.exths*/
                LOG_DIS("l.exths r%d, r%d\n", rd, ra);
                tcg_gen_ext16s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x01:   /*l.extbs*/
                LOG_DIS("l.extbs r%d, r%d\n", rd, ra);
                tcg_gen_ext8s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x02:   /*l.exthz*/
                LOG_DIS("l.exthz r%d, r%d\n", rd, ra);
                tcg_gen_ext16u_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x03:   /*l.extbz*/
                LOG_DIS("l.extbz r%d, r%d\n", rd, ra);
                tcg_gen_ext8u_tl(cpu_R[rd], cpu_R[ra]);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        break;

    case 0x000d:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:   /*l.extws*/
                LOG_DIS("l.extws r%d, r%d\n", rd, ra);
                tcg_gen_ext32s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x01:    /*l.extwz*/
                LOG_DIS("l.extwz r%d, r%d\n", rd, ra);
                tcg_gen_ext32u_tl(cpu_R[rd], cpu_R[ra]);
                break;
            default:
                break;
            }
        default:
            break;
        }
        break;

    default:
        break;
    }
}

static void dec_misc(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0, op1;
    uint32_t ra, rb, rd;
    uint32_t /*L6, K5, */I16, I5, I11, N26, tmp;
    op0 = field(insn, 26, 6);
    op1 = field(insn, 24, 2);
    ra = field(insn, 16, 5);
    rb = field(insn, 11, 5);
    rd = field(insn, 21, 5);
    /*L6 = field(insn, 5, 6);
    K5 = field(insn, 0, 5);*/
    I16 = field(insn, 0, 16);
    I5 = field(insn, 21, 5);
    I11 = field(insn, 0, 11);
    N26 = field(insn, 0, 26);
    tmp = (I5<<11) + I11;
    TCGv t0 = tcg_temp_new();

    switch (op0) {
    case 0x00:   /*l.j*/
        {
            LOG_DIS("l.j %d\n", N26);
            tmp = sign_extend((N26<<2), 26) + dc->pc;
            tcg_gen_movi_tl(jmp_pc, tmp);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
        }
        break;

    case 0x01:   /*l.jal*/
        {
            LOG_DIS("l.jal %d\n", N26);
            tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
            tmp = sign_extend((N26<<2), 26) + dc->pc;
            tcg_gen_movi_tl(jmp_pc, tmp);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
        }
        break;

    case 0x03:   /*l.bnf*/
        LOG_DIS("l.bnf %d\n", N26);
        {
            int lab = gen_new_label();
            TCGv sr_f = tcg_temp_new();
            tcg_gen_andi_tl(sr_f, cpu_sr, SR_F);
            tmp = sign_extend((N26 << 2), 26) + dc->pc;
            tcg_gen_movi_tl(jmp_pc, dc->pc+8);
            tcg_gen_brcondi_tl(TCG_COND_EQ, sr_f, SR_F, lab);
            tcg_gen_movi_tl(jmp_pc, tmp);
            gen_set_label(lab);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
            tcg_temp_free(sr_f);
        }
        break;

    case 0x04:   /*l.bf*/
        LOG_DIS("l.bf %d\n", N26);
        {
            int lab = gen_new_label();
            TCGv sr_f = tcg_temp_new();
            tcg_gen_andi_tl(sr_f, cpu_sr, SR_F);
            tmp = sign_extend((N26 << 2), 26) + dc->pc;
            tcg_gen_movi_tl(jmp_pc, dc->pc+8);
            tcg_gen_brcondi_tl(TCG_COND_NE, sr_f, SR_F, lab);
            tcg_gen_movi_tl(jmp_pc, tmp);
            gen_set_label(lab);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
            tcg_temp_free(sr_f);
        }
        break;

    case 0x05:
        switch (op1) {
        case 0x01:   /*l.nop*/
            LOG_DIS("l.nop %d\n", I16);
            break;
        default:
            break;
        }
        break;

    case 0x11:    /*l.jr*/
        LOG_DIS("l.jr r%d\n", rb);
        {
            tcg_gen_mov_tl(jmp_pc, cpu_R[rb]);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
        }
        break;

    case 0x12:    /*l.jalr*/
        LOG_DIS("l.jalr r%d\n", rb);
        {
            tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
            gen_load_gpr(jmp_pc, rb);
            dc->delayed_branch = 2;
            dc->tb_flags |= D_FLAG;
            tcg_gen_movi_i32(env_flags, dc->tb_flags);
        }
        break;

    case 0x13:    /*l.maci*/
        {
            LOG_DIS("l.maci %d, r%d, %d\n", I5, ra, I11);
            TCGv_i64 tra = tcg_temp_new_i64();    /*  store cpu_R[ra]*/
            TCGv_i64 tmac = tcg_temp_new_i64();   /*  store machi maclo*/
            tcg_gen_movi_tl(t0, tmp);
            tcg_gen_ext32s_i64(tra, cpu_R[ra]);
            tcg_gen_ext32s_i64(tmac, t0);
            tcg_gen_mul_tl(tmac, tra, tmac);
            tcg_gen_trunc_i64_i32(cpu_R[ra], tmac);
            tcg_gen_add_i64(machi, machi, cpu_R[ra]);
            tcg_gen_add_i64(maclo, maclo, cpu_R[ra]);
            tcg_temp_free(tra);
            tcg_temp_free(tmac);
        }
        break;

    case 0x09:    /*l.rfe*/
        LOG_DIS("l.rfe\n");
        {
            gen_helper_rfe(cpu_env);
            dc->is_jmp = DISAS_UPDATE;
        }
        break;

    case 0x1c:    /*l.cust1*/
        LOG_DIS("l.cust1\n");
        break;

    case 0x1d:    /*l.cust2*/
        LOG_DIS("l.cust2\n");
        break;

    case 0x1e:    /*l.cust3*/
        LOG_DIS("l.cust3\n");
        break;

    case 0x1f:    /*l.cust4*/
        LOG_DIS("l.cust4\n");
        break;

    case 0x3c:   /*l.cust5*/
        /*LOG_DIS("l.cust5 r%d, r%d, r%d, %d, %d\n", rd, ra, rb, L6, K5);*/
        break;

    case 0x3d:   /*l.cust6*/
        LOG_DIS("l.cust6\n");
        break;

    case 0x3e:   /*l.cust7*/
        LOG_DIS("l.cust7\n");
        break;

    case 0x3f:   /*l.cust8*/
        LOG_DIS("l.cust8\n");
        break;

    case 0x20:   /*l.ld*/
        LOG_DIS("l.ld r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld64(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x21:   /*l.lwz*/
        LOG_DIS("l.lwz r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld32u(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x22:   /*l.lws*/
        LOG_DIS("l.lws r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld32s(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x23:   /*l.lbz*/
        LOG_DIS("l.lbz r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld8u(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x24:   /*l.lbs*/
        LOG_DIS("l.lbs r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld8s(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x25:   /*l.lhz*/
        LOG_DIS("l.lhz r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld16u(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x26:   /*l.lhs*/
        LOG_DIS("l.lhs r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
        tcg_gen_qemu_ld16s(cpu_R[rd], t0, dc->mem_idx);
        break;

    case 0x27:   /*l.addi*/
        LOG_DIS("l.addi r%d, r%d, %d\n", rd, ra, I16);
        {
            TCGv t0 = tcg_const_tl(sign_extend(I16, 16));
            /* Aha, we do need a helper here for addi */
            tcg_temp_free(t0);
        }
        break;

    case 0x28:   /*l.addic*/
        LOG_DIS("l.addic r%d, r%d, %d\n", rd, ra, I16);
        {
            TCGv t0 = tcg_const_tl(sign_extend(I16, 16));
            /* Aha, we do need a helper here for addic */
            tcg_temp_free(t0);
        }
        break;

    case 0x29:   /*l.andi*/
        LOG_DIS("l.andi r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_andi_tl(cpu_R[rd], cpu_R[ra], zero_extend(I16, 16));
        break;

    case 0x2a:   /*l.ori*/
        LOG_DIS("l.ori r%d, r%d, %d\n", rd, ra, zero_extend(I16, 16));
        tcg_gen_ori_tl(cpu_R[rd], cpu_R[ra], I16);
        break;

    case 0x2b:   /*l.xori*/
        LOG_DIS("l.xori r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_xori_tl(cpu_R[rd], cpu_R[ra], sign_extend(I16, 16));
        break;

    case 0x2c:   /*l.muli*/
        LOG_DIS("l.muli r%d, r%d, %d\n", rd, ra, I16);
        if (ra != 0 && I16 != 0) {
            tcg_gen_muli_tl(cpu_R[rd], cpu_R[ra], I16);
            tcg_gen_ext32s_tl(cpu_R[rd], cpu_R[rd]);
        } else {
            tcg_gen_movi_tl(cpu_R[rd], 0);
        }
        break;

    case 0x2d:   /*l.mfspr*/
        LOG_DIS("l.mfspr r%d, r%d, %d\n", rd, ra, I16);
        {
            TCGv_i32 ti = tcg_const_i32(I16);
            TCGv td = tcg_const_tl(rd);
            TCGv ta = tcg_const_tl(ra);
            /* mfspr need a helper here */
            tcg_temp_free_i32(ti);
            tcg_temp_free(td);
            tcg_temp_free(ta);
        }
        break;

    case 0x30:  /*l.mtspr*/
        LOG_DIS("l.mtspr %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        {
            TCGv_i32 ti = tcg_const_i32(tmp);
            TCGv ta = tcg_const_tl(ra);
            TCGv tb = tcg_const_tl(rb);
            /* mtspr need a helper here */
            tcg_temp_free_i32(ti);
            tcg_temp_free(ta);
            tcg_temp_free(tb);
        }
        break;

    case 0x34:   /*l.sd*/
        LOG_DIS("l.sd %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
        tcg_gen_qemu_st64(cpu_R[rb], t0, dc->mem_idx);
        break;

    case 0x35:   /*l.sw*/
        LOG_DIS("l.sw %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
        tcg_gen_qemu_st32(cpu_R[rb], t0, dc->mem_idx);
        break;

    case 0x36:   /*l.sb*/
        LOG_DIS("l.sb %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
        tcg_gen_qemu_st8(cpu_R[rb], t0, dc->mem_idx);
        break;

    case 0x37:   /*l.sh*/
        LOG_DIS("l.sh %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
        tcg_gen_qemu_st16(cpu_R[rb], t0, dc->mem_idx);
        break;
    default:
        break;
    }
    tcg_temp_free(t0);
}

static void dec_mac(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;
    op0 = field(insn, 0, 4);
    ra = field(insn, 16, 5);
    rb = field(insn, 11, 5);
    TCGv_i64 t0 = tcg_temp_new();
    TCGv_i64 t1 = tcg_temp_new();
    switch (op0) {
    case 0x0001:   /*l.mac*/
        {
            LOG_DIS("l.mac r%d, r%d\n", ra, rb);
            tcg_gen_ext_i32_i64(t0, cpu_R[ra]);
            tcg_gen_ext_i32_i64(t1, cpu_R[rb]);
            tcg_gen_mul_i64(t0, t0, t1);
            tcg_gen_trunc_i64_i32(cpu_R[ra], t0);
            tcg_gen_add_tl(maclo, maclo, cpu_R[ra]);
            tcg_gen_add_tl(machi, machi, cpu_R[ra]);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;

    case 0x0002:   /*l.msb*/
        {
            LOG_DIS("l.msb r%d, r%d\n", ra, rb);
            tcg_gen_ext_i32_i64(t0, cpu_R[ra]);
            tcg_gen_ext_i32_i64(t1, cpu_R[rb]);
            tcg_gen_mul_i64(t0, t0, t1);
            tcg_gen_trunc_i64_i32(cpu_R[ra], t0);
            tcg_gen_sub_tl(maclo, maclo, cpu_R[ra]);
            tcg_gen_sub_tl(machi, machi, cpu_R[ra]);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;

    default:
        break;
   }
}

static void dec_logic(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd, ra, L6;
    op0 = field(insn, 6, 2);
    rd = field(insn, 21, 5);
    ra = field(insn, 16, 5);
    L6 = field(insn, 0, 6);

    switch (op0) {
    case 0x00:    /*l.slli*/
        LOG_DIS("l.slli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shli_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x01:    /*l.srli*/
        LOG_DIS("l.srli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x02:    /*l.srai*/
        LOG_DIS("l.srai r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_sari_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f)); break;

    case 0x03:    /*l.rori*/
        LOG_DIS("l.rori r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_rotri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    default:
        break;
    }
}

static void dec_M(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd;
    uint32_t K16;
    op0 = field(insn, 16, 1);
    rd = field(insn, 21, 5);
    K16 = field(insn, 0, 16);

    switch (op0) {
    case 0x0:    /*l.movhi*/
        LOG_DIS("l.movhi  r%d, %d\n", rd, K16);
        tcg_gen_movi_tl(cpu_R[rd], (K16 << 16));
        break;

    case 0x1:    /*l.macrc*/
        LOG_DIS("l.macrc  r%d\n", rd);
        tcg_gen_mov_tl(cpu_R[rd], maclo);
        tcg_gen_movi_tl(maclo, 0x0);
        tcg_gen_movi_tl(machi, 0x0);
        break;

    default:
        break;
    }
}

static void dec_comp(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;

    op0 = field(insn, 21, 5);
    ra = field(insn, 16, 5);
    rb = field(insn, 11, 5);

    tcg_gen_movi_i32(env_btaken, 0x0);
    /* unsigned integers  */
    tcg_gen_ext32u_tl(cpu_R[ra], cpu_R[ra]);
    tcg_gen_ext32u_tl(cpu_R[rb], cpu_R[rb]);

    switch (op0) {
    case 0x0:    /*l.sfeq*/
        LOG_DIS("l.sfeq  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_EQ, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1:    /*l.sfne*/
        LOG_DIS("l.sfne  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_NE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x2:    /*l.sfgtu*/
        LOG_DIS("l.sfgtu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GTU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x3:    /*l.sfgeu*/
        LOG_DIS("l.sfgeu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GEU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x4:    /*l.sfltu*/
        LOG_DIS("l.sfltu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LTU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x5:    /*l.sfleu*/
        LOG_DIS("l.sfleu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LEU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xa:    /*l.sfgts*/
        LOG_DIS("l.sfgts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xb:    /*l.sfges*/
        LOG_DIS("l.sfges  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xc:    /*l.sflts*/
        LOG_DIS("l.sflts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xd:    /*l.sfles*/
        LOG_DIS("l.sfles  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    default:
        break;
    }
    wb_SR_F();
}

static void dec_compi(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, I16;

    op0 = field(insn, 21, 5);
    ra = field(insn, 16, 5);
    I16 = field(insn, 0, 16);

    tcg_gen_movi_i32(env_btaken, 0x0);
    tcg_gen_ext32u_tl(cpu_R[ra], cpu_R[ra]);
    I16 = sign_extend(I16, 16);

    switch (op0) {
    case 0x0:    /*l.sfeqi*/
        LOG_DIS("l.sfeqi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_EQ, env_btaken, cpu_R[ra], I16);
        break;

    case 0x1:    /*l.sfnei*/
        LOG_DIS("l.sfnei  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_NE, env_btaken, cpu_R[ra], I16);
        break;

    case 0x2:    /*l.sfgtui*/
        LOG_DIS("l.sfgtui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GTU, env_btaken, cpu_R[ra], I16);
        break;

    case 0x3:    /*l.sfgeui*/
        LOG_DIS("l.sfgeui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GEU, env_btaken, cpu_R[ra], I16);
        break;

    case 0x4:    /*l.sfltui*/
        LOG_DIS("l.sfltui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LTU, env_btaken, cpu_R[ra],
                            sign_extend(I16, 16));
        break;

    case 0x5:    /*l.sfleui*/
        LOG_DIS("l.sfleui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LEU, env_btaken, cpu_R[ra], I16);
        break;

    case 0xa:    /*l.sfgtsi*/
        LOG_DIS("l.sfgtsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GT, env_btaken, cpu_R[ra], I16);
        break;

    case 0xb:    /*l.sfgesi*/
        LOG_DIS("l.sfgesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GE, env_btaken, cpu_R[ra], I16);
        break;

    case 0xc:    /*l.sfltsi*/
        LOG_DIS("l.sfltsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LT, env_btaken, cpu_R[ra], I16);
        break;

    case 0xd:    /*l.sflesi*/
        LOG_DIS("l.sflesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LE, env_btaken, cpu_R[ra], I16);
        break;

    default:
        break;
    }
    wb_SR_F();
}

static void dec_sys(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    /*uint32_t K16;*/
    op0 = field(insn, 16, 8);
    /*K16 = field(insn, 0, 16);*/

    switch (op0) {
    case 0x000:  /*l.sys*/
        /*LOG_DIS("l.sys %d\n", K16);*/
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_SYSCALL);
        dc->is_jmp = DISAS_UPDATE;
        break;

    case 0x100:  /*l.trap*/
        /*LOG_DIS("l.trap %d\n", K16);*/
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_TRAP);
        break;

    case 0x300:  /*l.csync*/
        LOG_DIS("l.csync\n");
        break;

    case 0x200:  /*l.msync*/
        LOG_DIS("l.msync\n");
        break;

    case 0x270:  /*l.psync*/
        LOG_DIS("l.psync\n");
        break;

    default:
        break;
    }
}

static void dec_float(DisasContext *dc, CPUOPENRISCState *env, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb, rd;
    op0 = field(insn, 0, 8);
    ra = field(insn, 16, 5);
    rb = field(insn, 11, 5);
    rd = field(insn, 21, 5);

    switch (op0) {
    case 0x10:    /*lf.add.d*/
        LOG_DIS("lf.add.d r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_add_i64(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x00:    /*lf.add.s*/
        LOG_DIS("lf.add.s r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_add_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x11:    /*lf.sub.d*/
        LOG_DIS("lf.sub.d r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_sub_i64(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x01:    /*lf.sub.s*/
        LOG_DIS("lf.sub.s r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_sub_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x12:    /*lf.mul.d*/
        LOG_DIS("lf.mul.d r%d, r%d, r%d\n", rd, ra, rb);
        {
            if (cpu_R[ra] != 0 && cpu_R[rb] != 0) {
                tcg_gen_mul_i64(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            } else {
                tcg_gen_movi_i64(cpu_R[rd], 0x0);
            }
        }
        break;

    case 0x02:    /*lf.mul.s*/
        LOG_DIS("lf.mul.s r%d, r%d, r%d\n", rd, ra, rb);
        if (cpu_R[ra] != 0 && cpu_R[rb] != 0) {
            tcg_gen_mul_i32(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        } else {
            tcg_gen_movi_i64(cpu_R[rd], 0x0);
        }
        break;

    case 0x13:    /*lf.div.d*/
        LOG_DIS("lf.div.d r%d, r%d, r%d\n", rd, ra, rb);
        if (cpu_R[rb] != 0) {
            tcg_gen_div_i64(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        } else {
            gen_exception(dc, EXCP_RANGE);
        }
        break;

    case 0x03:    /*lf.div.s*/
        LOG_DIS("lf.div.s r%d, r%d, r%d\n", rd, ra, rb);
        if (cpu_R[rb] != 0) {
            tcg_gen_div_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        } else {
            gen_exception(dc, EXCP_RANGE);
        }
        break;

    case 0x14:    /*lf.itof.d*/
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        /* itof.d need a helper here */
        break;

    case 0x04:    /*lf.itof.s*/
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        /* itof.s need a helper here */
        break;

    case 0x15:    /*lf.ftoi.d*/
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        /* ftoi.d need a helper here */
        break;

    case 0x05:    /*lf.ftoi.s*/
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        /* ftoi.s need a helper here */
        break;

    case 0x16:    /*lf.rem.d*/
        LOG_DIS("lf.rem.d r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_rem_i64(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x06:    /*lf.rem.s*/
        LOG_DIS("lf.rem.s r%d, r%d, r%d\n", rd, ra, rb);
        tcg_gen_rem_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
        break;

    case 0x17:    /*lf.madd.d*/
        LOG_DIS("lf.madd.d r%d, r%d, r%d\n", rd, ra, rb);
        {
            TCGv_i64 t0, t1;

            t0 = tcg_temp_new_i64();
            t1 = tcg_temp_new_i64();

            tcg_gen_ext_i32_i64(t0, cpu_R[ra]);
            tcg_gen_ext_i32_i64(t1, cpu_R[rb]);
            tcg_gen_mul_i64(t0, t0, t1);

            tcg_gen_trunc_i64_i32(cpu_R[ra], t0);
            tcg_gen_add_tl(fpmaddlo, cpu_R[ra], fpmaddlo);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_trunc_i64_i32(cpu_R[rb], t0);
            tcg_gen_add_tl(fpmaddhi, cpu_R[rb], fpmaddhi);

            tcg_temp_free_i64(t0);
            tcg_temp_free_i64(t1);
        }
        break;

    case 0x07:    /*lf.madd.s*/
        LOG_DIS("lf.madd.s r%d, r%d, r%d\n", rd, ra, rb);
        {
            TCGv_i64 t0, t1;

            t0 = tcg_temp_new_i64();
            t1 = tcg_temp_new_i64();

            tcg_gen_ext_i32_i64(t0, cpu_R[ra]);
            tcg_gen_ext_i32_i64(t1, cpu_R[rb]);
            tcg_gen_mul_i64(t0, t0, t1);

            tcg_gen_trunc_i64_i32(cpu_R[ra], t0);
            tcg_gen_add_tl(fpmaddlo, cpu_R[ra], fpmaddlo);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_trunc_i64_i32(cpu_R[rb], t0);
            tcg_gen_add_tl(fpmaddhi, cpu_R[rb], fpmaddhi);
            tcg_temp_free_i64(t0);
            tcg_temp_free_i64(t1);

        }
        break;

    case 0x18:    /*lf.sfeq.d*/
        LOG_DIS("lf.sfeq.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_GE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x08:    /*lf.sfeq.s*/
        LOG_DIS("lf.sfeq.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_EQ, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x19:    /*lf.sfne.d*/
        LOG_DIS("lf.sfne.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_NE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x09:    /*lf.sfne.s*/
        LOG_DIS("lf.sfne.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_NE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1a:    /*lf.sfgt.d*/
        LOG_DIS("lf.sfgt.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_GT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0a:    /*lf.sfgt.s*/
        LOG_DIS("lf.sfgt.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1b:    /*lf.sfge.d*/
        LOG_DIS("lf.sfge.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_GE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0b:    /*lf.sfge.s*/
        LOG_DIS("lf.sfge.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1c:    /*lf.sflt.d*/
        LOG_DIS("lf.sflt.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_LT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0c:    /*lf.sflt.s*/
        LOG_DIS("lf.sflt.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1d:    /*lf.sfle.d*/
        LOG_DIS("lf.sfle.d r%d, r%d\n", ra, rb);
        tcg_gen_setcond_i64(TCG_COND_LE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0d:    /*lf.sfle.s*/
        LOG_DIS("lf.sfle.s r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    default:
        break;
    }
    wb_SR_F();
}

static void disas_openrisc_insn(DisasContext *dc, CPUOPENRISCState *env)
{
    uint32_t op0;
    uint32_t insn;
    insn = ldl_code(dc->pc);
    op0 = field(insn, 26, 6);

    switch (op0) {
    case 0x06:
        dec_M(dc, env, insn);
        break;

    case 0x08:
        dec_sys(dc, env, insn);
        break;

    case 0x2e:
        dec_logic(dc, env, insn);
        break;

    case 0x2f:
        dec_compi(dc, env, insn);
        break;

    case 0x31:
        dec_mac(dc, env, insn);
        break;

    case 0x32:
        dec_float(dc, env, insn);
        break;

    case 0x38:
        dec_calc(dc, env, insn);
        break;

    case 0x39:
        dec_comp(dc, env, insn);
        break;

    default:
        dec_misc(dc, env, insn);
        break;
    }
}

static void check_breakpoint(CPUOPENRISCState *env, DisasContext *dc)
{
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
        QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                tcg_gen_movi_tl(cpu_pc, dc->pc);
                gen_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
            }
        }
    }
}

static inline void gen_intermediate_code_internal(CPUOPENRISCState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
    struct DisasContext ctx, *dc = &ctx;
    uint16_t *gen_opc_end;
    uint32_t pc_start;
    int j, lj;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    qemu_log_try_set_file(stderr);

    pc_start = tb->pc;
    dc->env = env;
    dc->tb = tb;

    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    dc->is_jmp = DISAS_NEXT;
    dc->ppc = pc_start;
    dc->pc = pc_start;
    dc->mem_idx = cpu_mmu_index(env);
    dc->tb_flags = tb->flags;
    dc->delayed_branch = !!(dc->tb_flags & D_FLAG);
    dc->singlestep_enabled = env->singlestep_enabled;
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("-----------------------------------------\n");
        log_cpu_state(env, 0);
    }

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_icount_start();

    do {
        check_breakpoint(env, dc);
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    gen_opc_instr_start[lj++] = 0;
                }
            }
            gen_opc_pc[lj] = dc->pc;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP))) {
            tcg_gen_debug_insn_start(dc->pc);
        }

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }
        dc->ppc = dc->pc - 4;
        dc->npc = dc->pc + 4;
        tcg_gen_movi_tl(cpu_ppc, dc->ppc);
        tcg_gen_movi_tl(cpu_npc, dc->npc);
        disas_openrisc_insn(dc, env);
        dc->pc = dc->npc;
        num_insns++;
        /* delay slot */
        if (dc->delayed_branch) {
            dc->delayed_branch--;
            if (!dc->delayed_branch) {
                dc->tb_flags &= ~D_FLAG;
                tcg_gen_movi_i32(env_flags, dc->tb_flags);
                tcg_gen_mov_tl(cpu_pc, jmp_pc);
                tcg_gen_mov_tl(cpu_npc, jmp_pc);
                tcg_gen_movi_tl(jmp_pc, 0);
                tcg_gen_exit_tb(0);
                dc->is_jmp = DISAS_JUMP;
                break;
            }
        }
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end
             && !env->singlestep_enabled
             && !singlestep
             && (dc->pc < next_page_start)
             && num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }
    if (dc->is_jmp == DISAS_NEXT) {
        dc->is_jmp = DISAS_UPDATE;
        tcg_gen_movi_tl(cpu_pc, dc->pc);
    }
    if (unlikely(env->singlestep_enabled)) {
        if (dc->is_jmp == DISAS_NEXT) {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }
        gen_exception(dc, EXCP_DEBUG);
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 0, dc->pc);
            break;
        default:
        case DISAS_JUMP:
            break;
        case DISAS_UPDATE:
            /* indicate that the hash table must be used
               to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }

    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j) {
            gen_opc_instr_start[lj++] = 0;
        }
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("\n");
        log_target_disas(pc_start, dc->pc - pc_start, 0);
        qemu_log("\nisize=%d osize=%td\n",
            dc->pc - pc_start, gen_opc_ptr - gen_opc_buf);
    }
#endif
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
    int i;
    uint32_t *regs = env->gpr;
    cpu_fprintf(f, "PC=%08x\n", env->pc);
    for (i = 0; i < 32; ++i) {
        cpu_fprintf(f, "R%02d=%08x%c", i, regs[i],
                    (i % 4) == 3 ? '\n' : ' ');
    }
}

void restore_state_to_opc(CPUOPENRISCState *env, TranslationBlock *tb,
                          int pc_pos)
{
    env->pc = gen_opc_pc[pc_pos];
}
