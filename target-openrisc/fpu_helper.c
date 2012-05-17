/*
 * OpenRISC float helper routines
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
#include "excp.h"

static inline int ieee_ex_to_openrisc(int xcpt)
{
    int ret = 0;

    if (xcpt) {
        if (xcpt & float_flag_invalid) {
            ret &= FPCSR_FPEE;
        }
        if (xcpt & float_flag_overflow) {
            ret |= FPCSR_OVF;
        }
        if (xcpt & float_flag_underflow) {
            ret |= FPCSR_UNF;
        }
        if (xcpt & float_flag_divbyzero) {
            ret |= FPCSR_DZF;
        }
    }

    return ret;
}

static inline void update_fpcsr(CPUOPENRISCState *env)
{
    int tmp = ieee_ex_to_openrisc(get_float_exception_flags(&env->fp_status));
    SET_FP_CAUSE(env->fpcsr, tmp);
    if (GET_FP_ENABLE(env->fpcsr) & tmp) {
        helper_exception(env, EXCP_FPE);
    } else {
      UPDATE_FP_FLAGS(env->fpcsr, tmp);
    }
}

target_ulong HELPER(itofd)(CPUOPENRISCState *env, target_ulong val)
{
    uint64_t itofd;
    set_float_exception_flags(0, &env->fp_status);
    itofd = int32_to_float64(val, &env->fp_status);
    update_fpcsr(env);
    return itofd;
}

target_ulong HELPER(itofs)(CPUOPENRISCState *env, target_ulong val)
{
    target_ulong itofs;
    set_float_exception_flags(0, &env->fp_status);
    itofs = int32_to_float32(val, &env->fp_status);
    update_fpcsr(env);
    return itofs;
}

target_ulong HELPER(ftoid)(CPUOPENRISCState *env, target_ulong val)
{
    target_ulong ftoid;
    set_float_exception_flags(0, &env->fp_status);
    ftoid = float32_to_int64(val, &env->fp_status);
    update_fpcsr(env);
    return ftoid;
}

target_ulong HELPER(ftois)(CPUOPENRISCState *env, target_ulong val)
{
    target_ulong ftois;
    set_float_exception_flags(0, &env->fp_status);
    ftois = float32_to_int32(val, &env->fp_status);
    update_fpcsr(env);
    return ftois;
}
