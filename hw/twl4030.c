/*
 * TI TWL4030 for beagle board
 *
 * Copyright (C) 2008 yajin<yajin@vm-kernel.org>
 * Copyright (C) 2009 Nokia Corporation
 *
 * Register implementation based on TPS65950 ES1.0 specification.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
 
#include "hw.h"
#include "qemu-timer.h"
#include "i2c.h"
#include "sysemu.h"
#include "console.h"
#include "cpu-all.h"

#define VERBOSE 1
#define TRACEW(regname, value) fprintf(stderr, "%s: %s = 0x%02x\n", __FUNCTION__, regname, value);

//extern CPUState *cpu_single_env;

struct twl4030_i2c_s {
    i2c_slave i2c;
    int firstbyte;
    uint8_t reg;
    qemu_irq irq;
    uint8 reg_data[256];
    struct twl4030_s *twl4030;
};

struct twl4030_s {
    struct twl4030_i2c_s *i2c[5];
    
    int key_cfg;
    int key_tst;
    
    uint8_t seq_mem[64][4]; /* power-management sequencing memory */
};

static const uint8_t addr_48_reset_values[256] = {
    0x51, 0x04, 0x02, 0xc0, 0x41, 0x41, 0x41, 0x10, /* 0x00...0x07 */
    0x10, 0x10, 0x06, 0x06, 0x06, 0x1f, 0x1f, 0x1f, /* 0x08...0x0f */
    0x1f, 0x1f, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x10...0x17 */
    0x00, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x00, /* 0x18...0x1f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0a, 0x03, /* 0x20...0x27 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x28...0x2f */
    0x00, 0x00, 0x00, 0x04, 0x04, 0x04, 0x00, 0x00, /* 0x30...0x37 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x38...0x3f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x40...0x47 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x48...0x4f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x50...0x57 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x58...0x5f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x60...0x67 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x68...0x6f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x70...0x77 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x78...0x7f */
    0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x00, 0x00, /* 0x80...0x87 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x88...0x8f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x90...0x97 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x98...0x9f */
    0x00, 0x10, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* 0xa0...0xa7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa8...0xaf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb0...0xb7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb8...0xb8 */
    0xa0, 0xa0, 0x64, 0x7f, 0x6c, 0x75, 0x64, 0x20, /* 0xc0...0xc7 */
    0x01, 0x17, 0x01, 0x02, 0x00, 0x36, 0x44, 0x07, /* 0xc8...0xcf */
    0x3b, 0x17, 0x6b, 0x04, 0x00, 0x00, 0x00, 0x00, /* 0xd0...0xd7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd8...0xdf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xe0...0xe7 */
    0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, /* 0xe8...0xef */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xf0...0xf7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00  /* 0xf8...0xff */
};

static const uint8_t addr_49_reset_values[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x00...0x07 */
    0x00, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x00, 0x00, /* 0x08...0x0f */
    0x3f, 0x3f, 0x3f, 0x3f, 0x25, 0x00, 0x00, 0x00, /* 0x10...0x17 */
    0x00, 0x32, 0x32, 0x32, 0x32, 0x00, 0x00, 0x55, /* 0x18...0x1f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20...0x27 */
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, /* 0x28...0x2f */
    0x13, 0x00, 0x00, 0x00, 0x00, 0x79, 0x11, 0x00, /* 0x30...0x37 */
    0x00, 0x00, 0x06, 0x00, 0x44, 0x69, 0x00, 0x00, /* 0x38...0x3f */
    0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, /* 0x40...0x47 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x48...0x4f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x50...0x57 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x58...0x5f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x60...0x67 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x68...0x6f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x70...0x77 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x78...0x7f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x80...0x87 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x88...0x8f */
    0x00, 0x90, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00, /* 0x90...0x97 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x98...0x9f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa0...0xa7 */
    0x00, 0x00, 0x04, 0x00, 0x55, 0x01, 0x55, 0x05, /* 0xa8...0xaf */
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x03, 0x00, /* 0xb0...0xb7 */
    0x00, 0x00, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, /* 0xb8...0xbf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /* 0xc0...0xc7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xc8...0xcf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd0...0xd7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd8...0xdf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xe0...0xe7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xe8...0xef */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xf0...0xf7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xf8...0xff */
};

static const uint8_t addr_4a_reset_values[256] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x00...0x07 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x08...0x0f */
    0xc0, 0x8c, 0xde, 0xde, 0x00, 0x00, 0x00, 0x00, /* 0x10...0x17 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x18...0x1f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20...0x27 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x28...0x2f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x30...0x37 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x38...0x3f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x40...0x47 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x48...0x4f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x50...0x57 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x58...0x5f */
    0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x55, 0x07, /* 0x60...0x67 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x68...0x6f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x70...0x77 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x78...0x7f */
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, /* 0x80...0x87 */
    0x00, 0x68, 0x9b, 0x86, 0x48, 0x2a, 0x07, 0x28, /* 0x88...0x8f */
    0x09, 0x69, 0x90, 0x00, 0x2a, 0x00, 0x02, 0x00, /* 0x90...0x97 */
    0x10, 0xcd, 0x02, 0x68, 0x03, 0x00, 0x00, 0x00, /* 0x98...0x9f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa0...0xa7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa8...0xaf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb0...0xb7 */
    0x00, 0x00, 0x00, 0xff, 0x0f, 0x00, 0x00, 0xff, /* 0xb8...0xbf */
    0x0f, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x01, 0x00, /* 0xc0...0xc7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xc8...0xcf */
    0x00, 0x00, 0x03, 0x00, 0x00, 0xe0, 0x00, 0x00, /* 0xd0...0xd7 */
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd8...0xdf */
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, /* 0xe0...0xe7 */
    0x55, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xe8...0xef */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xf0...0xf7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* 0xf8...0xff */
};

static const uint8_t addr_4b_reset_values[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x00...0x07 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x08...0x0f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x10...0x17 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, /* 0x18...0x1f */
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, /* 0x20...0x27 */
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x28...0x2f */
    0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0xbf, 0xbf, /* 0x30...0x37 */
    0xbf, 0xab, 0x00, 0x08, 0x3f, 0x15, 0x40, 0x0e, /* 0x38...0x3f */
    0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x40...0x47 */
    0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, /* 0x48...0x4f */
    0x00, 0x02, 0x00, 0x04, 0x0d, 0x00, 0x00, 0x00, /* 0x50...0x57 */
    0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x58...0x5f */
    0x00, 0x00, 0x2f, 0x18, 0x0f, 0x08, 0x0f, 0x08, /* 0x60...0x67 */
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x68...0x6f */
    0x00, 0x00, 0x00, 0x00, 0x08, 0x02, 0x80, 0x03, /* 0x70...0x77 */
    0x08, 0x09, 0x00, 0x00, 0x08, 0x03, 0x80, 0x03, /* 0x78...0x7f */
    0x08, 0x02, 0x00, 0x00, 0x08, 0x00, 0x80, 0x03, /* 0x80...0x87 */
    0x08, 0x08, 0x20, 0x00, 0x00, 0x02, 0x80, 0x04, /* 0x88...0x8f */
    0x08, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, /* 0x90...0x97 */
    0x08, 0x02, 0xe0, 0x01, 0x08, 0x00, 0xe0, 0x00, /* 0x98...0x9f */
    0x08, 0x01, 0xe0, 0x01, 0x08, 0x04, 0xe0, 0x03, /* 0xa0...0xa7 */
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa8...0xaf */
    0x20, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb0...0xb7 */
    0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, /* 0xb8...0xbf */
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, /* 0xc0...0xc7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, /* 0xc8...0xcf */
    0x00, 0x08, 0xe0, 0x00, 0x08, 0x00, 0x00, 0x00, /* 0xd0...0xd7 */
    0x14, 0x08, 0xe0, 0x02, 0x08, 0xe0, 0x00, 0x08, /* 0xd8...0xdf */
    0xe0, 0x05, 0x08, 0xe0, 0x06, 0x08, 0xe0, 0x00, /* 0xe0...0xe7 */
    0x08, 0xe0, 0x00, 0x08, 0xe0, 0x06, 0x06, 0xe0, /* 0xe8...0xef */
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xf0...0xf7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* 0xf8...0xff */
};

static uint8_t twl4030_48_read(void *opaque, uint8_t addr)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x00: /* VENDOR_ID_LO */
        case 0x01: /* VENDOR_ID_HI */
        case 0x02: /* PRODUCT_ID_LO */
        case 0x03: /* PRODUCT_ID_HI */
            return s->reg_data[addr];
        case 0x04: /* FUNC_CTRL */
        case 0x05: /* FUNC_CRTL_SET */
        case 0x06: /* FUNC_CRTL_CLR */
            return s->reg_data[0x04];
        case 0x07: /* IFC_CTRL */
        case 0x08: /* IFC_CRTL_SET */
        case 0x09: /* IFC_CRTL_CLR */
            return s->reg_data[0x07];
        case 0xac: /* POWER_CTRL */
        case 0xad: /* POWER_SET */
        case 0xae: /* POWER_CLR */
            return s->reg_data[0xac];
        case 0xfd: /* PHY_PWR_CTRL */
        case 0xfe: /* PHY_CLK_CTRL */
            return s->reg_data[addr];
        case 0xff: /* PHY_CLK_CTRL */
            return s->reg_data[0xfe] & 0x1;
        default:
#ifdef VERBOSE
            printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
	        //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
            exit(-1);
            break;
    }
}

static void twl4030_48_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x04: /* IFC_CTRL */
            s->reg_data[0x04] = value & 0x80;
            break;
        case 0x05: /* IFC_CRTL_SET */
            s->reg_data[0x04] =  (s->reg_data[0x04] | value) & 0x80;
            break;
        case 0x06: /* IFC_CRTL_CLEAR */
            s->reg_data[0x04] =  (s->reg_data[0x04] & ~value) & 0x80;
            break;
        case 0x07: /* IFC_CTRL */
            s->reg_data[0x07] = value & 0x61;
            break;
        case 0x08: /* IFC_CRTL_SET */
            s->reg_data[0x07] =  (s->reg_data[0x07] | value) & 0x61;
            break;
        case 0x09: /* IFC_CRTL_CLEAR */
            s->reg_data[0x07] =  (s->reg_data[0x07] & ~value) & 0x61;
            break;
        case 0xac: /* POWER_CTRL */
            s->reg_data[0xac] = value & 0x20;
            break;
        case 0xad: /* POWER_SET */
            s->reg_data[0xac] =  (s->reg_data[0xac] | value) & 0x20;
            break;
        case 0xae: /* POWER_CLEAR */
            s->reg_data[0xac] =  (s->reg_data[0xac] & ~value) & 0x20;
            break;
        case 0xfd: /* PHY_PWR_CTRL */
            s->reg_data[addr] = value & 0x1;
            break;
        case 0xfe: /* PHY_CLK_CTRL */
            s->reg_data[addr] = value & 0x7;
            break;
        default:
#ifdef VERBOSE
            printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
            //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
			exit(-1);
			break;
    }
}

static int twl4030_48_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_48_write(s, s->reg++, data);
	
    return 0;
}

static int twl4030_48_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_48_read(s, s->reg++);
}

static void twl4030_48_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
    memcpy(s->reg_data, addr_48_reset_values, 256);
}

static void twl4030_48_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_49_read(void *opaque, uint8_t addr)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;

    switch (addr) {
        case 0x98: /* GPIO_DATAIN1 */
        case 0x99: /* GPIO_DATAIN2 */
        case 0x9a: /* GPIO_DATAIN3 */
        case 0x9b: /* GPIO_DATADIR1 */
        case 0x9c: /* GPIO_DATADIR2 */
        case 0x9d: /* GPIO_DATADIR3 */
        case 0xb1: /* GPIO_ISR1A */
        case 0xb2: /* GPIO_ISR2A */
        case 0xb3: /* GPIO_ISR3A */
        case 0xc0: /* GPIO_EDR1 */
        case 0xc1: /* GPIO_EDR2 */
        case 0xc2: /* GPIO_EDR3 */
        case 0xc3: /* GPIO_EDR4 */
        case 0xc4: /* GPIO_EDR5 */
            return s->reg_data[addr];
        default:
#ifdef VERBOSE
            fprintf(stderr, "%s: unknown register %02x pc %x\n",
                    __FUNCTION__, addr,cpu_single_env->regs[15]);
#endif
			exit(-1);
    }
}

static void twl4030_49_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x9b: /* GPIODATADIR1 */
        case 0x9c: /* GPIODATADIR2 */
        case 0x9d: /* GPIODATADIR3 */
        case 0x9e: /* GPIODATAOUT1 */
        case 0x9f: /* GPIODATAOUT2 */
        case 0xa0: /* GPIODATAOUT3 */
        case 0xa1: /* CLEARGPIODATAOUT1 */
        case 0xa2: /* CLEARGPIODATAOUT2 */
        case 0xa3: /* CLEARGPIODATAOUT3 */
        case 0xa4: /* SETGPIODATAOUT1 */
        case 0xa5: /* SETGPIODATAOUT2 */
        case 0xa6: /* SETGPIODATAOUT3 */
        case 0xa7: /* GPIO_DEBEN1 */
        case 0xa8: /* GPIO_DEBEN2 */
        case 0xa9: /* GPIO_DEBEN3 */
        case 0xaa: /* GPIO_CTRL */
        case 0xab: /* GPIOPUPDCTR1 */
        case 0xac: /* GPIOPUPDCTR2 */
        case 0xad: /* GPIOPUPDCTR3 */
        case 0xae: /* GPIOPUPDCTR4 */
            s->reg_data[addr] = value;
            break;
        case 0xaf: /* GPIOPUPDCTR5 */
            s->reg_data[addr] = value & 0x0f;
            break;
	    case 0xb4: /* GPIO_IMR1A */
	    case 0xb5: /* GPIO_IMR2A */
            s->reg_data[addr] = value;
            break;
	    case 0xb6: /* GPIO_IMR3A */
            s->reg_data[addr] = value & 0x03;
            break;
	    case 0xc0: /* GPIO_EDR1 */
	    case 0xc1: /* GPIO_EDR2 */
	    case 0xc2: /* GPIO_EDR3 */
	    case 0xc3: /* GPIO_EDR4 */
	    case 0xc4: /* GPIO_EDR5 */
            s->reg_data[addr] = value;
            break;
	    case 0xc5: /* GPIO_SIH_CTRL */
            s->reg_data[addr] = value & 0x07;
            break;
        default:
#ifdef VERBOSE
            printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,
                   cpu_single_env->regs[15]);
            //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
            exit(-1);
            break;
    }
}


static int twl4030_49_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_49_write(s, s->reg++, data);
	
    return 0;
}

static int twl4030_49_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_49_read(s, s->reg++);
}

static void twl4030_49_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
    memcpy(s->reg_data, addr_49_reset_values, 256);
}

static void twl4030_49_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_4a_read(void *opaque, uint8_t addr)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x61: /* MADC_ISR1 */
        case 0xb9: /* BCIISR1A */
        case 0xba: /* BCIISR2A */
        case 0xe3: /* KEYP_ISR1 */
        case 0xee: /* LEDEN */
            return s->reg_data[addr];
        default:
#ifdef VERBOSE
	        printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
#endif
            exit(-1);
            break;
    }
}

static void twl4030_4a_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x61: /* MADC_ISR1 */
            s->reg_data[value] &= ~(value & 0x0f);
            break;
        case 0x62: /* MADC_IMR1 */
            s->reg_data[value] = value & 0x0f;
            break;
        case 0xb9: /* BCIISR1A */
            s->reg_data[value] &= ~value;
            break;
        case 0xba: /* BCIISR2A */
            s->reg_data[value] &= ~(value & 0x0f);
            break;
        case 0xbb: /* BCIIMR1A */
            s->reg_data[addr] = value;
            break;
        case 0xbc: /* BCIIMR2A */
            s->reg_data[addr] = value & 0x0f;
            break;
        case 0xe4: /* KEYP_IMR1 */
            s->reg_data[addr] = value & 0x0f;
            break;
        case 0xe9: /* KEYP_SIH_CTRL */
            s->reg_data[addr] = value & 0x07;
            break;
        case 0xee: /* LEDEN */
            s->reg_data[addr] = value;
#ifdef VERBOSE
            fprintf(stderr, "%s: LEDA power=%s/enable=%s, LEDB power=%s/enable=%s\n", __FUNCTION__,
                    value & 0x10 ? "on" : "off", value & 0x01 ? "yes" : "no",
                    value & 0x20 ? "on" : "off", value & 0x02 ? "yes" : "no");
#endif      
            break;
        case 0xef: /* PWMAON */
            s->reg_data[addr] = value;
            break;
        case 0xf0: /* PWMAOFF */
            s->reg_data[addr] = value & 0x7f;
            break;
        default:
#ifdef VERBOSE
	        printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
#endif
            exit(-1);
            break;
    }
}

static int twl4030_4a_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_4a_write(s, s->reg++, data);
	
    return 0;
}

static int twl4030_4a_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_4a_read(s, s->reg++);
}

static void twl4030_4a_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
    memcpy(s->reg_data, addr_4a_reset_values, 256);
}

static void twl4030_4a_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_4b_read(void *opaque, uint8_t addr)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    switch (addr) {
        case 0x1c: /* RTC */
        case 0x1d:
        case 0x1e:
        case 0x1f:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2b:
        case 0x2c:
        case 0x2d: /*RTC end */
        case 0x2e: /* PWR_ISR1 */
        case 0x33: /* PWR_EDR1 */
        case 0x34: /* PWR_EDR2 */
        case 0x45: /* STS_HW_CONDITIONS */
            return s->reg_data[addr];
        default:
#ifdef VERBOSE
	        printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
	        //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
            exit(-1);
            break;
    }
}


static void twl4030_4b_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    uint8_t seq_addr, seq_sub;
	
    switch (addr) {
        case 0x1c: /* SECONDS_REG */
        case 0x1d: /* MINUTES_REG */
        case 0x23: /* ALARM_SECONDS_REG */
        case 0x24: /* ALARM_MINUTES_REG */
            s->reg_data[addr] = value & 0x7f;
            break;
        case 0x1e: /* HOURS_REG */
        case 0x25: /* ALARM_HOURS_REG */
            s->reg_data[addr] = value & 0xbf;
            break;
        case 0x1f: /* DAYS_REG */
        case 0x26: /* ALARM_DAYS_REG */
            s->reg_data[addr] = value & 0x3f;
            break;
        case 0x20: /* MONTHS_REG */
        case 0x27: /* ALARM_MONTHS_REG */
            s->reg_data[addr] = value & 0x1f;
            break;
        case 0x21: /* YEARS_REG */
        case 0x28: /* ALARM_YEARS_REG */
            s->reg_data[addr] = value;
            break;
        case 0x22: /* WEEKS_REG */
            s->reg_data[addr] = value & 0x07;
            break;
        case 0x29: /* RTC_CTRL_REG */
            s->reg_data[addr] = value & 0x7f;
            break;
        case 0x2a: /* RTC_STATUS_REG */
            s->reg_data[addr] = value & 0xfe;
            break;
        case 0x2b: /* RTC_INTERRUPTS_REG */
            s->reg_data[addr] = value & 0x0f;
            break;
        case 0x2c: /* RTC_COMP_LSB_REG */
        case 0x2d: /* RTC_COMP_MSB_REG */
            s->reg_data[addr] = value;
            break;
        case 0x33: /* PWR_EDR1 */
        case 0x34: /* PWR_EDR2 */
            s->reg_data[addr] = value;
            break;
        case 0x46: /* P1_SW_EVENTS */
        case 0x47: /* P2_SW_EVENTS */
        case 0x48: /* P3_SW_EVENTS */
            s->reg_data[addr] = value & 0x78;
            break;
        case 0x52: /* SEQ_ADD_W2P */
        case 0x53: /* SEQ_ADD_P2A */
        case 0x54: /* SEQ_ADD_A2W */
        case 0x55: /* SEQ_ADD_A2S */
        case 0x56: /* SEQ_ADD_S2A12 */
        case 0x57: /* SEQ_ADD_S2A3 */
        case 0x58: /* SEQ_ADD_WARM */
            if (s->twl4030->key_cfg)
                s->reg_data[addr] = value & 0x3f;
            break;
        case 0x59: /* MEMORY_ADDRESS */
            if (s->twl4030->key_cfg)
                s->reg_data[addr] = value;
            break;
        case 0x5a: /* MEMORY_DATA */
            if (s->twl4030->key_cfg) {
                s->reg_data[addr] = value;
                seq_addr = s->reg_data[0x59];
                seq_sub = seq_addr & 3;
                seq_addr >>= 2;
                if ((seq_addr >= 0x2b && seq_addr <= 0x3e) || (seq_addr <= 0x0e && seq_sub == 3))
                    s->twl4030->seq_mem[seq_addr][seq_sub] = value;
            }
            s->reg_data[0x59]++; /* TODO: check if autoincrement is write-protected as well */
            break;
        case 0x7a: /* VAUX3_DEV_GRP */
        case 0x82: /* VMMC1_DEV_GRP */
        case 0x8e: /* VPLL2_DEV_GRP */
        case 0x96: /* VDAC_DEV_GRP */
        case 0xcc: /* VUSB1V5_DEV_GRP */
        case 0xcf: /* VUSB1V8_DEV_GRP */
        case 0xd2: /* VUSB3V1_DEV_GRP */
        case 0xe6: /* HFCLKOUT_DEV_GRP */
            s->reg_data[addr] = (s->reg_data[addr] & 0x0f) | (value & 0xf0); 
            break;
        case 0x2f: /* PWR_IMR1 */
            s->reg_data[addr] = value;
            break;
        case 0x35: /* PWR_SIH_CTRL */
            s->reg_data[addr] = value & 0x07;
            break;
        case 0x3b: /* CFG_BOOT */
            if (s->twl4030->key_cfg)
                s->reg_data[addr] = (s->reg_data[addr] & 0x70) | (value & 0x8f);
            break;
        case 0x44: /* PROTECT_KEY */
            s->twl4030->key_cfg = 0;
            s->twl4030->key_tst = 0;
            switch (value) {
                case 0x0C: 
                    if (s->reg_data[addr] == 0xC0)
                        s->twl4030->key_cfg = 1;
                    break;
                case 0xE0:
                    if (s->reg_data[addr] == 0x0E)
                        s->twl4030->key_tst = 1;
                    break;
                case 0xEC:
                    if (s->reg_data[addr] == 0xCE) {
                        s->twl4030->key_cfg = 1;
                        s->twl4030->key_tst = 1;
                    }
                    break;
                default:
                    break;
            }
            s->reg_data[addr] = value;
            break;
        case 0x7d: /* VAUX3_DEDICATED */
            if (s->twl4030->key_tst)
                s->reg_data[addr] = value & 0x77;
            else
                s->reg_data[addr] = (s->reg_data[addr] & 0x70) | (value & 0x07);
            break;
        case 0x85: /* VMMC1_DEDICATED */
        case 0x99: /* VDAC_DEDICATED */
            if (s->twl4030->key_tst) 
                s->reg_data[addr] = value & 0x73;
            else
                s->reg_data[addr] = (s->reg_data[addr] & 0x70) | (value & 0x03);
            break;
        case 0x91: /* VPLL2_DEDICATED */
            if (s->twl4030->key_tst)
                s->reg_data[addr] = value & 0x7f;
            else
                s->reg_data[addr] = (s->reg_data[addr] & 0x70) | (value & 0x0f);
            break;
        case 0xcd: /* VUSB1V5_TYPE */
        case 0xd0: /* VUSB1V8_TYPE */
        case 0xd3: /* VUSB3V1_TYPE */
            s->reg_data[addr] = value & 0x1f;
            break;
        case 0xd8: /* VUSB_DEDICATED1 */
            s->reg_data[addr] = value & 0x1f;
            break;
        case 0xd9: /* VUSB_DEDICATED2 */
            s->reg_data[addr] = value & 0x08;
            break;
            
        default:
	        fprintf(stderr, "%s: unknown register %02x value %0x pc %x \n", __FUNCTION__, 
                    addr, value, cpu_single_env->regs[15]);
            exit(-1);
            break;
    }
}

static int twl4030_4b_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_4b_write(s, s->reg++, data);
	
    return 1;
}

static int twl4030_4b_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_4b_read(s, s->reg++);
}

static void twl4030_4b_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
    memcpy(s->reg_data, addr_4b_reset_values, 256);
    s->twl4030->key_cfg = 0;
    s->twl4030->key_tst = 0;
}

static void twl4030_4b_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

struct twl4030_s *twl4030_init(i2c_bus *bus, qemu_irq irq)
{
    int i;
	
    struct twl4030_s *s = (struct twl4030_s *) qemu_mallocz(sizeof(*s));
	
    if (!s)
    {
        fprintf(stderr,"can not alloc memory space for twl4030_s \n");
        exit(-1);
    }
    for (i=0;i<5;i++)
    {
        s->i2c[i]=(struct twl4030_i2c_s *)i2c_slave_init(bus, 0, sizeof(struct twl4030_i2c_s));
        s->i2c[i]->irq = irq;
        s->i2c[i]->twl4030 = s;
    }
    s->i2c[0]->i2c.event = twl4030_48_event;
    s->i2c[0]->i2c.recv = twl4030_48_rx;
    s->i2c[0]->i2c.send = twl4030_48_tx;
    twl4030_48_reset(&s->i2c[0]->i2c);
    i2c_set_slave_address((i2c_slave *)&s->i2c[0]->i2c,0x48);
	
    s->i2c[1]->i2c.event = twl4030_49_event;
    s->i2c[1]->i2c.recv = twl4030_49_rx;
    s->i2c[1]->i2c.send = twl4030_49_tx;
    twl4030_49_reset(&s->i2c[1]->i2c);
    i2c_set_slave_address((i2c_slave *)&s->i2c[1]->i2c,0x49);
	
    s->i2c[2]->i2c.event = twl4030_4a_event;
    s->i2c[2]->i2c.recv = twl4030_4a_rx;
    s->i2c[2]->i2c.send = twl4030_4a_tx;
    twl4030_4a_reset(&s->i2c[2]->i2c);
    i2c_set_slave_address((i2c_slave *)&s->i2c[2]->i2c,0x4a);
	
    s->i2c[3]->i2c.event = twl4030_4b_event;
    s->i2c[3]->i2c.recv = twl4030_4b_rx;
    s->i2c[3]->i2c.send = twl4030_4b_tx;
    twl4030_4b_reset(&s->i2c[3]->i2c);
    i2c_set_slave_address((i2c_slave *)&s->i2c[3]->i2c,0x4b);
    /*TODO:other group*/
	
	
    //register_savevm("menelaus", -1, 0, menelaus_save, menelaus_load, s);
    return s;
}

#if 0
static uint8_t twl4030_read(void *opaque, uint8_t addr)
{
//    struct twl4030_s *s = (struct twl4030_s *) opaque;
//    int reg = 0;

    printf("twl4030_read addr %x\n",addr);

    switch (addr)
    {
    	default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __FUNCTION__, addr);
#endif
			//exit(-1);
			break;
    }
    return 0x00;
}

static void twl4030_write(void *opaque, uint8_t addr, uint8_t value)
{
//    struct twl4030_s *s = (struct twl4030_s *) opaque;
//    int line;
//    int reg = 0;
//    struct tm tm;

    printf("twl4030_write addr %x value %x \n",addr,value);

     switch (addr) 
     {
     	 case 0x82:
     	 case 0x85:
     	 	/*mmc*/
     	 	break;
        default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __FUNCTION__, addr);
#endif
			//exit(-1);
			break;
     	}
}


static int twl4030_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_s *s = (struct twl4030_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_write(s, s->reg ++, data);

    return 0;
}

static int twl4030_rx(i2c_slave *i2c)
{
    struct twl4030_s *s = (struct twl4030_s *) i2c;

    return twl4030_read(s, s->reg ++);
}

static void twl4030_reset(i2c_slave *i2c)
{
    struct twl4030_s *s = (struct twl4030_s *) i2c;
    s->reg = 0x00;
}

static void twl4030_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_s *s = (struct twl4030_s *) i2c;

    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

i2c_slave *twl4030_init(i2c_bus *bus, qemu_irq irq)
{
    struct twl4030_s *s = (struct twl4030_s *)
            i2c_slave_init(bus, 0, sizeof(struct twl4030_s));

    s->i2c.event = twl4030_event;
    s->i2c.recv = twl4030_rx;
    s->i2c.send = twl4030_tx;

    s->irq = irq;
    //s->rtc.hz_tm = qemu_new_timer(rt_clock, menelaus_rtc_hz, s);
    //s->in = qemu_allocate_irqs(menelaus_gpio_set, s, 3);
    //s->pwrbtn = qemu_allocate_irqs(menelaus_pwrbtn_set, s, 1)[0];

    twl4030_reset(&s->i2c);

    //register_savevm("menelaus", -1, 0, menelaus_save, menelaus_load, s);

    return &s->i2c;
}
#endif
 

