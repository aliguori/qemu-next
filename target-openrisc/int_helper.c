/*
 * OpenRISC int helper routines
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

target_ulong HELPER(ff1)(target_ulong x)
{
    target_ulong n = 0;

    if (x == 0) {
        return 0;
    }

    for (n = 32; x; n--) {
        x <<= 1;
    }
    return n+1;
}

target_ulong HELPER(fl1)(target_ulong x)
{
    target_ulong n = 0;

    if (x == 0) {
        return 0;
    }

    for (n = 0; x; n++) {
        x >>= 1;
    }
    return n;
}

/* l.add, l.addc, l.addi, l.addic, l.sub.  for overflow exception.  */
target_ulong HELPER(add)(CPUOPENRISCState * env, target_ulong a, target_ulong b)
{
    target_ulong result;
    result = a + b;

    if (result < a) {
        env->sr |= SR_CY;
    } else {
        env->sr &= ~SR_CY;
    }

    if ((a ^ b ^ -1) & (a ^ result)) {
        env->sr |= SR_OV;
        if (env->sr & SR_OVE) {
            raise_exception(env, EXCP_RANGE);
        }
    } else {
        env->sr &= ~SR_OV;
    }
    return result;
}

target_ulong HELPER(addc)(CPUOPENRISCState * env,
                          target_ulong a, target_ulong b)
{
    target_ulong result;
    int cf = env->sr & SR_CY;

    if (!cf) {
        result = a + b;
        cf = result < a;
    } else {
        result = a + b + 1;
        cf = result <= a;
    }

    if (cf) {
        env->sr |= SR_CY;
    } else {
        env->sr &= ~SR_CY;
    }

    if ((a ^ b ^ -1) & (a ^ result)) {
        env->sr |= SR_OV;
        if (env->sr & SR_OVE) {
            raise_exception(env, EXCP_RANGE);
        }
    } else {
        env->sr &= ~SR_OV;
    }
    return result;
}

target_ulong HELPER(sub)(CPUOPENRISCState * env, target_ulong a, target_ulong b)
{
    target_ulong result;
    result = a - b;
    if (a >= b) {
        env->sr |= SR_CY;
    } else {
        env->sr &= ~SR_CY;
    }

    if ((a ^ b) & (a ^ result)) {
        env->sr |= SR_OV;
        if (env->sr & SR_OVE) {
            raise_exception(env, EXCP_RANGE);
        }
    } else {
        env->sr &= ~SR_OV;
    }
    return result;
