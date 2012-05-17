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

#ifndef QEMU_OPENRISC_CPU_QOM_H
#define QEMU_OPENRISC_CPU_QOM_H

#include "qemu/cpu.h"
#include "cpu.h"

#define TYPE_OPENRISC_CPU "or32"

#define OPENRISC_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(OPENRISCCPUClass, (klass), TYPE_OPENRISC_CPU)
#define OPENRISC_CPU(obj) \
    OBJECT_CHECK(OPENRISCCPU, (obj), TYPE_OPENRISC_CPU)
#define OPENRISC_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(OPENRISCCPUClass, (obj), TYPE_OPENRISC_CPU)

/**
 * OPENRISCCPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * A Openrisc CPU model.
 */
typedef struct OPENRISCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    void (*parent_reset)(CPUState *cpu);
} OPENRISCCPUClass;

/**
 * OPENRISCCPU:
 * @env: #CPUOPENRISCState
 *
 * A Openrisc CPU.
 */
typedef struct OPENRISCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUOPENRISCState env;
} OPENRISCCPU;

static inline OPENRISCCPU *openrisc_env_get_cpu(CPUOPENRISCState *env)
{
    return OPENRISC_CPU(container_of(env, OPENRISCCPU, env));
}

#define ENV_GET_CPU(e) CPU(openrisc_env_get_cpu(e))

#endif /* QEMU_OPENRISC_CPU_QOM_H */
