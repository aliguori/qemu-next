/*
 * QEMU PC APM controller Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifndef PC_APM_H
#define PC_APM_H

#include <stdint.h>
#include "qemu-common.h"

typedef struct APMState {
    uint8_t apmc;
    uint8_t apms;
} APMState;

typedef void (*apm_ctrl_changed_t)(uint32_t val, void *arg);
void apm_init(APMState *s, apm_ctrl_changed_t callback, void *arg);

void apm_save(QEMUFile *f, APMState *apm);
void apm_load(QEMUFile *f, APMState *apm);

#endif /* PC_APM_H */
