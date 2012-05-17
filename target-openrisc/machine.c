/*
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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

#include "hw/hw.h"
#include "hw/boards.h"
#include "kvm.h"

static const VMStateDescription vmstate_cpu = {
    .name = "cpu",
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpr, CPUOPENRISCState, 32),
        VMSTATE_UINT32(sr, CPUOPENRISCState),
        VMSTATE_UINT32(epcr, CPUOPENRISCState),
        VMSTATE_UINT32(eear, CPUOPENRISCState),
        VMSTATE_UINT32(esr, CPUOPENRISCState),
        VMSTATE_UINT32(fpcsr, CPUOPENRISCState),
        VMSTATE_UINT32(pc, CPUOPENRISCState),
        VMSTATE_UINT32(npc, CPUOPENRISCState),
        VMSTATE_UINT32(ppc, CPUOPENRISCState),
        VMSTATE_END_OF_LIST()
    }
};

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUOPENRISCState *env = (CPUOPENRISCState *)opaque;
    unsigned int i;

    for (i = 0; i < 32; i++) {
        qemu_put_betls(f, &env->gpr[i]);
    }

    qemu_put_betls(f, &env->epcr);
    qemu_put_betls(f, &env->eear);
    qemu_put_betls(f, &env->esr);

    qemu_put_betls(f, &env->sr);
    qemu_put_be32s(f, &env->pc);
    qemu_put_be32s(f, &env->fpcsr);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUOPENRISCState *env = (CPUOPENRISCState *)opaque;
    unsigned int i;

    for (i = 0; i < 32; i++) {
        qemu_get_betls(f, &env->gpr[i]);
    }

    qemu_get_betls(f, &env->epcr);
    qemu_get_betls(f, &env->eear);
    qemu_get_betls(f, &env->esr);

    qemu_get_betls(f, &env->sr);
    qemu_get_betls(f, &env->pc);
    qemu_get_be32s(f, &env->fpcsr);
    tlb_flush(env, 1);

    return 0;
}
