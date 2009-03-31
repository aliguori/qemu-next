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
#define TRACE(fmt, ...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, __##VA_ARGS__)
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
        case 0xff: /* PHY_CLK_CTRL */
            return s->reg_data[0xfe] & 0x1;
        default:
            fprintf(stderr, "%s: unknown register 0x%02x pc %x\n",
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
            fprintf(stderr, "%s: unknown register 0x%02x pc %x\n",
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
        case 0x01: /* CODEC_MODE */
        case 0x02: /* OPTION */
        case 0x04: /* MICBIAS_CTL */
        case 0x05: /* ANAMICL */
        case 0x06: /* ANAMICR */
        case 0x07: /* AVADC_CTL */
        case 0x08: /* ADCMICSEL */
        case 0x09: /* DIGMIXING */
        case 0x0a: /* ATXL1PGA */
        case 0x0b: /* ATXR1PGA */
        case 0x0c: /* AVTXL2PGA */
        case 0x0d: /* AVTXR2PGA */
        case 0x0e: /* AUDIO_IF */
        case 0x0f: /* VOICE_IF */
        case 0x10: /* ARXR1PGA */
        case 0x11: /* ARXL1PGA */
        case 0x12: /* ARXR2PGA */
        case 0x13: /* ARXL2PGA */
        case 0x14: /* VRXPGA */
        case 0x15: /* VSTPGA */
        case 0x16: /* VRX2ARXPGA */
        case 0x17: /* AVDAC_CTL */
        case 0x18: /* ARX2VTXPGA */
        case 0x19: /* ARXL1_APGA_CTL */
        case 0x1a: /* ARXR1_APGA_CTL */
        case 0x1b: /* ARXL2_APGA_CTL */
        case 0x1c: /* ARXR2_APGA_CTL */
        case 0x1d: /* ATX2ARXPGA */
        case 0x1e: /* BT_IF */
        case 0x1f: /* BTPGA */
        case 0x20: /* BTSTPGA */
        case 0x21: /* EAR_CTL */
        case 0x22: /* HS_SEL */
        case 0x23: /* HS_GAIN_SET */
        case 0x24: /* HS_POPN_SET */
        case 0x25: /* PREDL_CTL */
        case 0x26: /* PREDR_CTL */
        case 0x27: /* PRECKL_CTL */
        case 0x28: /* PRECKR_CTL */
        case 0x29: /* HFL_CTL */
        case 0x2a: /* HFR_CTL */
        case 0x2b: /* ALC_CTL */
        case 0x2c: /* ALC_SET1 */
        case 0x2d: /* ALC_SET2 */
        case 0x2e: /* BOOST_CTL */
        case 0x2f: /* SOFTVOL_CTL */
        case 0x30: /* DTMF_FREQSEL */
        case 0x31: /* DTMF_TONEXT1H */
        case 0x32: /* DTMF_TONEXT1L */
        case 0x33: /* DTMF_TONEXT2H */
        case 0x34: /* DTMF_TONEXT2L */
        case 0x35: /* DTMF_TONOFF */
        case 0x36: /* DTMF_WANONOFF */
        case 0x37: /* CODEC_RX_SCRAMBLE_H */
        case 0x38: /* CODEC_RX_SCRAMBLE_M */
        case 0x39: /* CODEC_RX_SCRAMBLE_L */
        case 0x3a: /* APLL_CTL */
        case 0x3b: /* DTMF_CTL */
        case 0x3c: /* DTMF_PGA_CTL2 */
        case 0x3d: /* DTMF_PGA_CTL1 */
        case 0x3e: /* MISC_SET_1 */
        case 0x3f: /* PCMBTMUX */
        case 0x43: /* RX_PATH_SEL */
        case 0x44: /* VDL_APGA_CTL */
        case 0x45: /* VIBRA_CTL */
        case 0x46: /* VIBRA_SET */
        case 0x48: /* ANAMIC_GAIN */
        case 0x49: /* MISC_SET_2 */
        /* Test region */
        case 0x4c: /* AUDIO_TEST_CTL */
        case 0x4d: /* INT_TEST_CTL */
        case 0x4e: /* DAC_ADC_TEST_CTL */
        case 0x4f: /* RXTX_TRIM_IB */
        case 0x50: /* CLD_CONTROL */
        case 0x51: /* CLD_MODE_TIMING */
        case 0x52: /* CLD_TRIM_RAMP */
        case 0x53: /* CLD_TESTV_CTL */
        case 0x54: /* APLL_TEST_CTL */
        case 0x55: /* APLL_TEST_DIV */
        case 0x56: /* APLL_TEST_CTL2 */
        case 0x57: /* APLL_TEST_CUR */
        case 0x58: /* DIGIMIC_BIAS1_CTL */
        case 0x59: /* DIGIMIC_BIAS2_CTL */
        case 0x5a: /* RX_OFFSET_VOICE */
        case 0x5b: /* RX_OFFSET_AL1 */
        case 0x5c: /* RX_OFFSET_AR1 */
        case 0x5d: /* RX_OFFSET_AL2 */
        case 0x5e: /* RX_OFFSET_AR2 */
        case 0x5f: /* OFFSET1 */
        case 0x60: /* OFFSET2 */
        /* PIH region */
        case 0x81: /* PIH_ISR_P1 */
        case 0x82: /* PIH_ISR_P2 */
        case 0x83: /* PIH_SIR */
        /* INTBR region */
        case 0x85: /* IDCODE_7_0 */
        case 0x86: /* IDCODE_15_8 */
        case 0x87: /* IDCODE_23_16 */
        case 0x88: /* IDCODE_31_24 */
        case 0x89: /* DIEID_7_0 */
        case 0x8a: /* DIEID_15_8 */
        case 0x8b: /* DIEID_23_16 */
        case 0x8c: /* DIEID_31_24 */
        case 0x8d: /* DIEID_39_32 */
        case 0x8e: /* DIEID_47_40 */
        case 0x8f: /* DIEID_55_48 */
        case 0x90: /* DIEID_63_56 */
        case 0x91: /* GPBR1 */
        case 0x92: /* PMBR1 */
        case 0x93: /* PMBR2 */
        case 0x94: /* GPPUPDCTR1 */
        case 0x95: /* GPPUPDCTR2 */
        case 0x96: /* GPPUPDCTR3 */
        case 0x97: /* UNLOCK_TEST_REG */
        /* GPIO region */
        case 0x98: /* GPIO_DATAIN1 */
        case 0x99: /* GPIO_DATAIN2 */
        case 0x9a: /* GPIO_DATAIN3 */
        case 0x9b: /* GPIO_DATADIR1 */
        case 0x9c: /* GPIO_DATADIR2 */
        case 0x9d: /* GPIO_DATADIR3 */
        case 0x9e: /* GPIO_DATAOUT1 */
        case 0x9f: /* GPIO_DATAOUT2 */
        case 0xa0: /* GPIO_DATAOUT3 */
        case 0xa1: /* GPIO_CLEARGPIODATAOUT1 */
        case 0xa2: /* GPIO_CLEARGPIODATAOUT2 */
        case 0xa3: /* GPIO_CLEARGPIODATAOUT3 */
        case 0xa4: /* GPIO_SETGPIODATAOUT1 */
        case 0xa5: /* GPIO_SETGPIODATAOUT2 */
        case 0xa6: /* GPIO_SETGPIODATAOUT3 */
        case 0xa7: /* GPIO_DEBEN1 */
        case 0xa8: /* GPIO_DEBEN2 */
        case 0xa9: /* GPIO_DEBEN3 */
        case 0xaa: /* GPIO_CTRL */
        case 0xab: /* GPIO_PUPDCTR1 */
        case 0xac: /* GPIO_PUPDCTR2 */
        case 0xad: /* GPIO_PUPDCTR3 */
        case 0xae: /* GPIO_PUPDCTR4 */
        case 0xaf: /* GPIO_PUPDCTR5 */
        case 0xb0: /* GPIO_TEST */
        case 0xb1: /* GPIO_ISR1A */
        case 0xb2: /* GPIO_ISR2A */
        case 0xb3: /* GPIO_ISR3A */
        case 0xb4: /* GPIO_IMR1A */
        case 0xb5: /* GPIO_IMR2A */
        case 0xb6: /* GPIO_IMR3A */
        case 0xb7: /* GPIO_ISR1B */
        case 0xb8: /* GPIO_ISR2B */
        case 0xb9: /* GPIO_ISR3B */
        case 0xba: /* GPIO_IMR1B */
        case 0xbb: /* GPIO_IMR2B */
        case 0xbc: /* GPIO_IMR3B */
        case 0xbd: /* GPIO_SIR1 */
        case 0xbe: /* GPIO_SIR2 */
        case 0xbf: /* GPIO_SIR3 */
        case 0xc0: /* GPIO_EDR1 */
        case 0xc1: /* GPIO_EDR2 */
        case 0xc2: /* GPIO_EDR3 */
        case 0xc3: /* GPIO_EDR4 */
        case 0xc4: /* GPIO_EDR5 */
        case 0xc5: /* GPIO_SIH_CTRL */
            return s->reg_data[addr];
        default:
            fprintf(stderr, "%s: unknown register 0x%02x pc %x\n",
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
        case 0x01: /* CODEC_MODE */
        case 0x02: /* OPTION */
        case 0x04: /* MICBIAS_CTL */
        case 0x05: /* ANAMICL */
        case 0x06: /* ANAMICR */
        case 0x07: /* AVADC_CTL */
        case 0x08: /* ADCMICSEL */
        case 0x09: /* DIGMIXING */
        case 0x0a: /* ATXL1PGA */
        case 0x0b: /* ATXR1PGA */
        case 0x0c: /* AVTXL2PGA */
        case 0x0d: /* AVTXR2PGA */
        case 0x0e: /* AUDIO_IF */
        case 0x0f: /* VOICE_IF */
        case 0x10: /* ARXR1PGA */
        case 0x11: /* ARXL1PGA */
        case 0x12: /* ARXR2PGA */
        case 0x13: /* ARXL2PGA */
        case 0x14: /* VRXPGA */
        case 0x15: /* VSTPGA */
        case 0x16: /* VRX2ARXPGA */
        case 0x17: /* AVDAC_CTL */
        case 0x18: /* ARX2VTXPGA */
        case 0x19: /* ARXL1_APGA_CTL */
        case 0x1a: /* ARXR1_APGA_CTL */
        case 0x1b: /* ARXL2_APGA_CTL */
        case 0x1c: /* ARXR2_APGA_CTL */
        case 0x1d: /* ATX2ARXPGA */
        case 0x1e: /* BT_IF */
        case 0x1f: /* BTPGA */
        case 0x20: /* BTSTPGA */
        case 0x21: /* EAR_CTL */
        case 0x22: /* HS_SEL */
        case 0x23: /* HS_GAIN_SET */
        case 0x24: /* HS_POPN_SET */
        case 0x25: /* PREDL_CTL */
        case 0x26: /* PREDR_CTL */
        case 0x27: /* PRECKL_CTL */
        case 0x28: /* PRECKR_CTL */
        case 0x29: /* HFL_CTL */
        case 0x2a: /* HFR_CTL */
        case 0x2b: /* ALC_CTL */
        case 0x2c: /* ALC_SET1 */
        case 0x2d: /* ALC_SET2 */
        case 0x2e: /* BOOST_CTL */
        case 0x2f: /* SOFTVOL_CTL */
        case 0x30: /* DTMF_FREQSEL */
        case 0x31: /* DTMF_TONEXT1H */
        case 0x32: /* DTMF_TONEXT1L */
        case 0x33: /* DTMF_TONEXT2H */
        case 0x34: /* DTMF_TONEXT2L */
        case 0x35: /* DTMF_TONOFF */
        case 0x36: /* DTMF_WANONOFF */
        case 0x37: /* CODEC_RX_SCRAMBLE_H */
        case 0x38: /* CODEC_RX_SCRAMBLE_M */
        case 0x39: /* CODEC_RX_SCRAMBLE_L */
        case 0x3a: /* APLL_CTL */
        case 0x3b: /* DTMF_CTL */
        case 0x3c: /* DTMF_PGA_CTL2 */
        case 0x3d: /* DTMF_PGA_CTL1 */
        case 0x3e: /* MISC_SET_1 */
        case 0x3f: /* PCMBTMUX */
        case 0x43: /* RX_PATH_SEL */
        case 0x44: /* VDL_APGA_CTL */
        case 0x45: /* VIBRA_CTL */
        case 0x46: /* VIBRA_SET */
        case 0x48: /* ANAMIC_GAIN */
        case 0x49: /* MISC_SET_2 */
            s->reg_data[addr] = value;
            break;
        /* Test region */
        case 0x4c: /* AUDIO_TEST_CTL */
        case 0x4d: /* INT_TEST_CTL */
        case 0x4e: /* DAC_ADC_TEST_CTL */
        case 0x4f: /* RXTX_TRIM_IB */
        case 0x50: /* CLD_CONTROL */
        case 0x51: /* CLD_MODE_TIMING */
        case 0x52: /* CLD_TRIM_RAMP */
        case 0x53: /* CLD_TESTV_CTL */
        case 0x54: /* APLL_TEST_CTL */
        case 0x55: /* APLL_TEST_DIV */
        case 0x56: /* APLL_TEST_CTL2 */
        case 0x57: /* APLL_TEST_CUR */
        case 0x58: /* DIGIMIC_BIAS1_CTL */
        case 0x59: /* DIGIMIC_BIAS2_CTL */
            s->reg_data[addr] = value;
            break;
        case 0x5a: /* RX_OFFSET_VOICE */
        case 0x5b: /* RX_OFFSET_AL1 */
        case 0x5c: /* RX_OFFSET_AR1 */
        case 0x5d: /* RX_OFFSET_AL2 */
        case 0x5e: /* RX_OFFSET_AR2 */
        case 0x5f: /* OFFSET1 */
        case 0x60: /* OFFSET2 */
            /* read-only, ignore */
            break;
        /* PIH region */
        case 0x81: /* PIH_ISR_P1 */
        case 0x82: /* PIH_ISR_P2 */
        case 0x83: /* PIH_SIR */
            s->reg_data[addr] = value;
            break;
        /* INTBR region */
        case 0x85: /* IDCODE_7_0 */
        case 0x86: /* IDCODE_15_8 */
        case 0x87: /* IDCODE_23_16 */
        case 0x88: /* IDCODE_31_24 */
        case 0x89: /* DIEID_7_0 */
        case 0x8a: /* DIEID_15_8 */
        case 0x8b: /* DIEID_23_16 */
        case 0x8c: /* DIEID_31_24 */
        case 0x8d: /* DIEID_39_32 */
        case 0x8e: /* DIEID_47_40 */
        case 0x8f: /* DIEID_55_48 */
        case 0x90: /* DIEID_63_56 */
            /* read-only, ignore */
            break;
        case 0x91: /* GPBR1 */
        case 0x92: /* PMBR1 */
        case 0x93: /* PMBR2 */
        case 0x94: /* GPPUPDCTR1 */
        case 0x95: /* GPPUPDCTR2 */
        case 0x96: /* GPPUPDCTR3 */
        case 0x97: /* UNLOCK_TEST_REG */
            s->reg_data[addr] = value;
            break;
        /* GPIO region */
        case 0x98: /* GPIODATAIN1 */
        case 0x99: /* GPIODATAIN2 */
        case 0x9a: /* GPIODATAIN3 */
            /* read-only, ignore */
            break;
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
        case 0xb0: /* GPIO_TEST */
        case 0xb1: /* GPIO_ISR1A */
        case 0xb2: /* GPIO_ISR2A */
        case 0xb3: /* GPIO_ISR3A */
	    case 0xb4: /* GPIO_IMR1A */
	    case 0xb5: /* GPIO_IMR2A */
            s->reg_data[addr] = value;
            break;
	    case 0xb6: /* GPIO_IMR3A */
            s->reg_data[addr] = value & 0x03;
            break;
        case 0xb7: /* GPIO_ISR1B */
        case 0xb8: /* GPIO_ISR2B */
        case 0xb9: /* GPIO_ISR3B */
        case 0xba: /* GPIO_IMR1B */
        case 0xbb: /* GPIO_IMR2B */
        case 0xbc: /* GPIO_IMR3B */
        case 0xbd: /* GPIO_SIR1 */
        case 0xbe: /* GPIO_SIR2 */
        case 0xbf: /* GPIO_SIR3 */
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
            fprintf(stderr, "%s: unknown register 0x%02x pc %x\n",
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
        case 0x61: /* MADC_ISR1 */
        case 0xb9: /* BCIISR1A */
        case 0xba: /* BCIISR2A */
        case 0xe3: /* KEYP_ISR1 */
        case 0xee: /* LEDEN */
            return s->reg_data[addr];
        default:
	        fprintf(stderr, "%s: unknown register %02x pc %x\n",
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
	        fprintf(stderr, "%s: unknown register %02x pc %x\n",
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
            return s->reg_data[addr];
        case 0x45: /* STS_HW_CONDITIONS - USB plugged, no VBUS -> host usb */
            return 0x4;
        default:
	        fprintf(stderr, "%s: unknown register %02x pc %x \n",
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
	        fprintf(stderr, "%s: unknown register %02x value %0x pc %x\n",
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
