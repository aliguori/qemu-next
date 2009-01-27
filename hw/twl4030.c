/*
 * TI TWL4030 for beagle board
 *
 * Copyright (C) 2008 yajin<yajin@vm-kernel.org>
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

#define VERBOSE 1

struct twl4030_s {
    i2c_slave i2c;
    qemu_irq irq;

    int firstbyte;
    uint8_t reg;

};
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

 

