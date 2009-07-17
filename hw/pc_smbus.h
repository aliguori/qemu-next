/*
 * QEMU PC SMBUS controller Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *
 *  This is based on piix_pci.c, but heavily modified.
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
#ifndef PC_SMBUS_H
#define PC_SMBUS_H

typedef struct PCSMBus {
    i2c_bus *smbus;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;
} PCSMBus;

void pc_smbus_init(PCSMBus *smb);
void smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t smb_ioport_readb(void *opaque, uint32_t addr);

#endif /* !PC_SMBUS_H */
