/*
 * Generic  OPENRISC Programmable Interrupt Controller support.
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

#include "hw.h"
#include "openrisc_cpudev.h"
#include "cpu.h"

/* Reset PIC */
void cpu_openrisc_pic_reset(CPUOPENRISCState *env)
{
    env->picmr = 0x00000000;
    env->picsr = 0x00000000;
}

/* openrisc pic handler */
static void openrisc_pic_cpu_handler(void *opaque, int irq, int level)
{
    CPUOPENRISCState *env = (CPUOPENRISCState *)opaque;
    int i;
    uint32_t irq_bit = 1 << irq;

    if (irq > 31 || irq < 0) {
        return;
    }

    if (level) {
        env->picsr |= irq_bit;
    } else {
        env->picsr &= ~irq_bit;
    }

    for (i = 0; i < 32; i++) {
        if ((env->picsr && (1 << i)) && (env->picmr && (1 << i))) {
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
            env->picsr &= ~(1 << i);
        }
    }
}

void cpu_openrisc_pic_init(CPUOPENRISCState *env)
{
    int i;
    qemu_irq *qi;
    qi = qemu_allocate_irqs(openrisc_pic_cpu_handler, env, NR_IRQS);

    for (i = 0; i < NR_IRQS; i++) {
        env->irq[i] = qi[i];
    }
}

void cpu_openrisc_store_picmr(CPUOPENRISCState *env, uint32_t value)
{
    env->picmr |= value;
}

void cpu_openrisc_store_picsr(CPUOPENRISCState *env, uint32_t value)
{
    env->picsr &= ~value;
}
