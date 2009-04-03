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

//#define VERBOSE 1

#ifdef VERBOSE
#define TRACE(fmt, ...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

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
	
    TRACE("addr=0x%02x", addr);
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
        case 0xff: /* PHY_CLK_CTRL_STS */
            if (s->reg_data[0xfe] & 1) /* REQ_PHY_DPLL_CLK */
                return 1;
            return (s->reg_data[0x04] >> 6) & 1; /* SUSPENDM */
        default:
            fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15]);
            break;
    }
    return 0;
}

static void twl4030_48_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    TRACE("addr=0x%02x, value=0x%02x", addr, value);
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
            fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15]);
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

    TRACE("addr=0x%02x", addr);
    switch (addr) {
        /* AUDIO_VOICE region */
        case 0x01 ... 0x49:
            return s->reg_data[addr];
        /* Test region */
        case 0x4c ... 0x60:
            return s->reg_data[addr];
        /* PIH region */
        case 0x81: /* PIH_ISR_P1 */
        case 0x82: /* PIH_ISR_P2 */
        case 0x83: /* PIH_SIR */
            return s->reg_data[addr];
        /* INTBR region */
        case 0x85 ... 0x97:
            return s->reg_data[addr];
        /* GPIO region */
        case 0x98 ... 0xc5:
            return s->reg_data[addr];
        default:
            fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15]);
			break;
    }
    return 0;
}

static void twl4030_49_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
	
    TRACE("addr=0x%02x, value=0x%02x", addr, value);
    switch (addr) {
        /* AUDIO_VOICE region */
        case 0x01 ... 0x49:
            s->reg_data[addr] = value;
            break;
        /* Test region */
        case 0x4c ... 0x59:
            s->reg_data[addr] = value;
            break;
        case 0x5a ... 0x60:
            /* read-only, ignore */
            break;
        /* PIH region */
        case 0x81: /* PIH_ISR_P1 */
        case 0x82: /* PIH_ISR_P2 */
        case 0x83: /* PIH_SIR */
            s->reg_data[addr] = value;
            break;
        /* INTBR region */
        case 0x85 ... 0x90:
            /* read-only, ignore */
            break;
        case 0x91 ... 0x97:
            s->reg_data[addr] = value;
            break;
        /* GPIO region */
        case 0x98 ... 0x9a:
            /* read-only, ignore */
            break;
        case 0x9b ... 0xae:
            s->reg_data[addr] = value;
            break;
        case 0xaf: /* GPIOPUPDCTR5 */
            s->reg_data[addr] = value & 0x0f;
            break;
        case 0xb0 ... 0xb5:
            s->reg_data[addr] = value;
            break;
	    case 0xb6: /* GPIO_IMR3A */
            s->reg_data[addr] = value & 0x03;
            break;
        case 0xb7 ... 0xc4:
            s->reg_data[addr] = value;
            break;
	    case 0xc5: /* GPIO_SIH_CTRL */
            s->reg_data[addr] = value & 0x07;
            break;
        default:
            fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15]);
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
	
    TRACE("addr=0x%02x", addr);
    switch (addr) {
        /* MADC region */
        case 0x00 ... 0x67:
            return s->reg_data[addr];
        /* MAIN_CHARGE region */
        case 0x74 ... 0xa6:
            return s->reg_data[addr];
        /* Interrupt region */
        case 0xb9 ... 0xc6:
            return s->reg_data[addr];
        /* KEYPAD region */
        case 0xd2 ... 0xe9:
            return s->reg_data[addr];
        /* LED region */
        case 0xee: /* LEDEN */
            return s->reg_data[addr];
        /* PWMA region */
        case 0xef: /* PWMAON */
        case 0xf0: /* PWMAOFF */
            return s->reg_data[addr];
        /* PWMB region */
        case 0xf1: /* PWMBON */
        case 0xf2: /* PWMBOFF */
            return s->reg_data[addr];
        /* PWM0 region */
        case 0xf8: /* PWM0ON */
        case 0xf9: /* PWM0OFF */
            return s->reg_data[addr];
        /* PWM1 region */
        case 0xfb: /* PWM1ON */
        case 0xfc: /* PWM1OFF */
            return s->reg_data[addr];
        default:
	        fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15] );
            break;
    }
    return 0;
}

static void twl4030_4a_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;

    TRACE("addr=0x%02x, value=0x%02x", addr, value);
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
            TRACE("LEDA power=%s/enable=%s, LEDB power=%s/enable=%s",
                    value & 0x10 ? "on" : "off", value & 0x01 ? "yes" : "no",
                    value & 0x20 ? "on" : "off", value & 0x02 ? "yes" : "no");
            break;
        case 0xef: /* PWMAON */
            s->reg_data[addr] = value;
            break;
        case 0xf0: /* PWMAOFF */
            s->reg_data[addr] = value & 0x7f;
            break;
        default:
	        fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15]);
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

	TRACE("addr=0x%02x", addr);
    switch (addr) {
        /* SECURED_REG region */
        case 0x00 ... 0x13:
            return s->reg_data[addr];
        /* BACKUP_REG region */
        case 0x14 ... 0x1b:
            return s->reg_data[addr];
        /* RTC region */
        case 0x1c ... 0x2d:
            return s->reg_data[addr];
        /* INT region */
        case 0x2e ... 0x35:
            return s->reg_data[addr];
        /* PM_MASTER region */
        case 0x36 ... 0x44:
            return s->reg_data[addr];
        case 0x45: /* STS_HW_CONDITIONS - USB plugged, no VBUS -> host usb */
            return 0x4;
        case 0x46 ... 0x5a:
            return s->reg_data[addr];
        /* PM_RECEIVER region */
        case 0x5b ... 0xf1: 
            return s->reg_data[addr];
        default:
	        fprintf(stderr, "%s: unknown register 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, cpu_single_env->regs[15] );
            break;
    }
    return 0;
}


static void twl4030_4b_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    uint8_t seq_addr, seq_sub;

	TRACE("addr=0x%02x, value=0x%02x", addr, value);
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
                if ((seq_addr >= 0x2b && seq_addr <= 0x3e) ||
                    (seq_addr <= 0x0e && seq_sub == 3))
                    s->twl4030->seq_mem[seq_addr][seq_sub] = value;
            }
            /* TODO: check if autoincrement is write-protected as well */
            s->reg_data[0x59]++; 
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
	        fprintf(stderr,
                    "%s: unknown register 0x%02x value 0x%02x pc 0x%x\n",
                    __FUNCTION__, addr, value, cpu_single_env->regs[15]);
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

static void twl4030_save_state(QEMUFile *f, void *opaque)
{
    struct twl4030_s *s = (struct twl4030_s *)opaque;
    int i;
    
    qemu_put_sbe32(f, s->key_cfg);
    qemu_put_sbe32(f, s->key_tst);
    for (i = 0; i < 64; i++)
        qemu_put_buffer(f, s->seq_mem[i], 4);
    for (i = 0; i < 5; i++) {
        qemu_put_sbe32(f, s->i2c[i]->firstbyte);
        qemu_put_byte(f, s->i2c[i]->reg);
        qemu_put_buffer(f, s->i2c[i]->reg_data, sizeof(s->i2c[i]->reg_data));
    }
}

static int twl4030_load_state(QEMUFile *f, void *opaque, int version_id)
{
    struct twl4030_s *s = (struct twl4030_s *)opaque;
    int i;
    
    if (version_id)
        return -EINVAL;
    
    s->key_cfg = qemu_get_sbe32(f);
    s->key_tst = qemu_get_sbe32(f);
    for (i = 0; i < 64; i++)
        qemu_get_buffer(f, s->seq_mem[i], 4);
    for (i = 0; i < 5; i++) {
        s->i2c[i]->firstbyte = qemu_get_sbe32(f);
        s->i2c[i]->reg = qemu_get_byte(f);
        qemu_get_buffer(f, s->i2c[i]->reg_data, sizeof(s->i2c[i]->reg_data));
    }
    
    return 0;
}

struct twl4030_s *twl4030_init(i2c_bus *bus, qemu_irq irq)
{
    int i;
	
    struct twl4030_s *s = (struct twl4030_s *) qemu_mallocz(sizeof(*s));
	
    for (i = 0; i < 5; i++) {
        s->i2c[i]=(struct twl4030_i2c_s *)i2c_slave_init(
            bus, 0, sizeof(struct twl4030_i2c_s));
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
	
    register_savevm("twl4030", -1, 0,
                    twl4030_save_state, twl4030_load_state, s);

    return s;
}
