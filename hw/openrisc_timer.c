/*
 *  QEMU openrisc timer support
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

#include "hw.h"
#include "openrisc_cpudev.h"
#include "qemu-timer.h"

#define TIMER_FREQ    (20 * 1000 * 1000)    /* 20MHz */

/* The time when ttcr changes */
static uint64_t last_clk;
static int is_counting;

/* Timer Mode */
enum {
    TIMER_NONE = (0<<30),
    TIMER_INTR = (1<<30),
    TIMER_SHOT = (2<<30),
    TIMER_CONT = (3<<30),
};

static void count_update(CPUOPENRISCState *env)
{
    uint64_t now, next;
    uint32_t wait;

    now = qemu_get_clock_ns(vm_clock);
    if (!is_counting) {
        qemu_del_timer(env->timer);
        last_clk = now;
        return;
    }

    env->ttcr += (uint32_t)muldiv64(now - last_clk, TIMER_FREQ,
                                    get_ticks_per_sec());
    last_clk = now;

    if ((env->ttmr & TTMR_TP) <= (env->ttcr & TTMR_TP)) {
        wait = TTMR_TP - (env->ttcr & TTMR_TP) + 1;
        wait += env->ttmr & TTMR_TP;
    } else {
        wait = (env->ttmr & TTMR_TP) - (env->ttcr & TTMR_TP);
    }

    next = now + muldiv64(wait, get_ticks_per_sec(), TIMER_FREQ);
    qemu_mod_timer(env->timer, next);
}

static void count_start(CPUOPENRISCState *env)
{
    is_counting = 1;
    count_update(env);
}

static void count_stop(CPUOPENRISCState *env)
{
    is_counting = 0;
    count_update(env);
}

uint32_t cpu_openrisc_get_count(CPUOPENRISCState *env)
{
    count_update(env);
    return env->ttcr;
}

void cpu_openrisc_store_count(CPUOPENRISCState *env, uint32_t count)
{
    /* Store new count register */
    env->ttcr = count;
    if (env->ttmr & TIMER_NONE) {
        return;
    }
    count_start(env);
}

void cpu_openrisc_store_compare(CPUOPENRISCState *env, uint32_t value)
{
    int ip = env->ttmr & TTMR_IP;

    if (value & TTMR_IP) { /* Keep IP bit */
        env->ttmr = (value & ~TTMR_IP) + ip;
    } else {               /* Clear IP bit */
        env->ttmr = value & ~TTMR_IP;
        env->interrupt_request &= ~CPU_INTERRUPT_TIMER;
    }
    count_update(env);

    switch (env->ttmr & TTMR_M) {
    case TIMER_NONE:
        count_stop(env);
        break;
    case TIMER_INTR:
        count_start(env);
        break;
    case TIMER_SHOT:
        count_start(env);
        break;
    case TIMER_CONT:
        count_start(env);
        break;
    }
}

static void openrisc_timer_cb(void *opaque)
{
    CPUOPENRISCState *env = opaque;

    if ((env->ttmr & TTMR_IE) &&
         qemu_timer_expired(env->timer, qemu_get_clock_ns(vm_clock))) {
        env->ttmr |= TTMR_IP;
        env->interrupt_request |= CPU_INTERRUPT_TIMER;
    }

    switch (env->ttmr & TTMR_M) {
    case TIMER_NONE:
        break;
    case TIMER_INTR:
        env->ttcr = 0;
        count_start(env);
        break;
    case TIMER_SHOT:
        count_stop(env);
        break;
    case TIMER_CONT:
        count_start(env);
        break;
    }
}

void cpu_openrisc_clock_init(CPUOPENRISCState *env)
{
    env->timer = qemu_new_timer_ns(vm_clock, &openrisc_timer_cb, env);
    env->ttmr = 0;
    env->ttcr = 0;
}
