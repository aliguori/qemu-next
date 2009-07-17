/*
 * QEMU PC APM controller Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *
 * This is split out from acpi.c
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

#include "pc_apm.h"
#include "hw.h"
#include "isa.h"

//#define DEBUG

/* fixed I/O location */
#define APM_CNT_IOPORT  0xb2
#define APM_STS_IOPORT  0xb3

static void *apm_arg;
static apm_ctrl_changed_t apm_callback;

static void apm_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    APMState *apm = opaque;
    addr &= 1;
#ifdef DEBUG
    printf("apm_ioport_writeb addr=0x%x val=0x%02x\n", addr, val);
#endif
    if (addr == 0) {
        apm->apmc = val;

        if (apm_callback) {
            apm_callback(val, apm_arg);
        }
    } else {
        apm->apms = val;
    }
}

static uint32_t apm_ioport_readb(void *opaque, uint32_t addr)
{
    APMState *apm = opaque;
    uint32_t val;

    addr &= 1;
    if (addr == 0) {
        val = apm->apmc;
    } else {
        val = apm->apms;
    }
#ifdef DEBUG
    printf("apm_ioport_readb addr=0x%x val=0x%02x\n", addr, val);
#endif
    return val;
}

void apm_save(QEMUFile *f, APMState *apm)
{
    qemu_put_8s(f, &apm->apmc);
    qemu_put_8s(f, &apm->apms);
}

void apm_load(QEMUFile *f, APMState *apm)
{
    qemu_get_8s(f, &apm->apmc);
    qemu_get_8s(f, &apm->apms);
}

void apm_init(APMState *apm, apm_ctrl_changed_t callback, void *arg)
{
    apm_callback = callback;
    apm_arg = arg;

    /* ioport 0xb2, 0xb3 */
    register_ioport_write(APM_CNT_IOPORT, 2, 1, apm_ioport_writeb, apm);
    register_ioport_read(APM_CNT_IOPORT, 2, 1, apm_ioport_readb, apm);
}
