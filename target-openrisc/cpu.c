/*
 *  QEMU Openrisc CPU
 *
 *  Copyright (c) 2012 Jia Liu <proljc@gmail.com>
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
#include "cpu-qom.h"
#include "qemu-common.h"


/* CPUClass::reset() */
static void openrisc_cpu_reset(CPUState *s)
{
    OPENRISCCPU *cpu = OPENRISC_CPU(s);
    OPENRISCCPUClass *occ = OPENRISC_CPU_GET_CLASS(cpu);
    CPUOPENRISCState *env = &cpu->env;

    occ->parent_reset(s);

    openrisc_reset(env);

}

static void openrisc_cpu_initfn(Object *obj)
{
    OPENRISCCPU *cpu = OPENRISC_CPU(obj);
    CPUOPENRISCState *env = &cpu->env;

    cpu_exec_init(env);

    env->flags = 0;

    cpu_reset(CPU(cpu));
}

static void openrisc_cpu_class_init(ObjectClass *oc, void *data)
{
    OPENRISCCPUClass *occ = OPENRISC_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    occ->parent_reset = cc->reset;
    cc->reset = openrisc_cpu_reset;
}

static const TypeInfo openrisc_cpu_type_info = {
    .name = TYPE_OPENRISC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(OPENRISCCPU),
    .instance_init = openrisc_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(OPENRISCCPUClass),
    .class_init = openrisc_cpu_class_init,
};

static void openrisc_cpu_register_types(void)
{
    type_register_static(&openrisc_cpu_type_info);
}

type_init(openrisc_cpu_register_types)
