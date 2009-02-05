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
#include "cpu-all.h"

#define VERBOSE 1

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
};


static uint8_t twl4030_48_read(void *opaque, uint8_t addr)
{
    //struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int reg = 0;
	
    printf("twl4030_48_read addr %x\n",addr);
	
    switch (addr)
    {
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
    //struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int line;
    //int reg = 0;
    //struct tm tm;
	
    printf("twl4030_48_write addr %x value %x \n",addr,value);
    
    switch (addr)
    {
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
        twl4030_48_write(s, s->reg ++, data);
	
    return 0;
}

static int twl4030_48_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_48_read(s, s->reg ++);
}

static void twl4030_48_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
}

static void twl4030_48_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_49_read(void *opaque, uint8_t addr)
{
    //struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int reg = 0;
	
    printf("twl4030_49_read addr %x\n",addr);
	
    switch (addr)
    {
        default:
#ifdef VERBOSE
            printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
            //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
			exit(-1);
			break;
    }
}

static void twl4030_49_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int line;
    //int reg = 0;
    //struct tm tm;
	
    //printf("twl4030_49_write addr %x value %x \n", addr, value);
	
    switch (addr)
    {
        case 0xaa:
        case 0xab:
        case 0xac:
        case 0xad:
        case 0xae:
        case 0xaf:
            fprintf(stderr,"%s: addr %x value %02x\n", __FUNCTION__, addr, value);
            /* fallthrough */
	    case 0xb4:  /*GPIO IMR*/
	    case 0xb5:
	    case 0xb6:
	    case 0xb7:
	    case 0xb8:
	    case 0xb9:
	    case 0xba:
	    case 0xbb:
	    case 0xbc:
	    case 0xc5:
	    	s->reg_data[addr] = value;
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
        twl4030_49_write(s, s->reg ++, data);
	
    return 0;
}

static int twl4030_49_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_49_read(s, s->reg ++);
}

static void twl4030_49_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
}

static void twl4030_49_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_4a_read(void *opaque, uint8_t addr)
{
    //struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int reg = 0;
	
    printf("twl4030_4a_read addr %x\n",addr);
	
    switch (addr)
    {
        default:
#ifdef VERBOSE
	        printf("%s: unknown register %02x pc %x \n", __FUNCTION__, addr,cpu_single_env->regs[15] );
            //printf("%s: unknown register %02x \n", __FUNCTION__, addr);
#endif
            exit(-1);
            break;
    }
}

static void twl4030_4a_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int line;
    //int reg = 0;
    //struct tm tm;
	
    fprintf(stderr, "%s: addr %x value %02x\n", __FUNCTION__, addr, value);
	
    switch (addr)
    {
        case 0xee:  /*LED EN*/
        case 0xe4:
        case 0xe9:
        case 0xbb:
        case 0xbc:
        case 0x62:
            
        case 0x61:
        case 0xb9:
        case 0xba:
        case 0xef:
        case 0xf0:
            s->reg_data[addr] = value;
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

static int twl4030_4a_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_4a_write(s, s->reg ++, data);
	
    return 0;
}

static int twl4030_4a_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_4a_read(s, s->reg ++);
}

static void twl4030_4a_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
}

static void twl4030_4a_event(i2c_slave *i2c, enum i2c_event event)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static uint8_t twl4030_4b_read(void *opaque, uint8_t addr)
{
    //struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) opaque;
    //int reg = 0;
	
    printf("twl4030_4b_read addr %x\n",addr);
	
    switch (addr)
    {
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
	
    fprintf(stderr, "%s: addr %x value %02x\n", __FUNCTION__, addr, value);
	
    switch (addr)
    {
        case 0x2f:
        case 0x35:
        case 0x3b:
        case 0x44:
        case 0x82:
        case 0x85:
        case 0x7a:
        case 0x7d:
        case 0x8e:
        case 0x91:
        case 0x96:
        case 0x99:

        case 0x46:
        case 0x47:
        case 0x48:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x59:
        case 0x5a:
        case 0xcc:
        case 0xcd:
        case 0xcf:
        case 0xd0:
        case 0xd2:
        case 0xd3:
        case 0xd8:
        case 0xd9:
        case 0xe6:
            s->reg_data[addr] = value;
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

static int twl4030_4b_tx(i2c_slave *i2c, uint8_t data)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        twl4030_4b_write(s, s->reg ++, data);
	
    return 1;
}

static int twl4030_4b_rx(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
	
    return twl4030_4b_read(s, s->reg ++);
}

static void twl4030_4b_reset(i2c_slave *i2c)
{
    struct twl4030_i2c_s *s = (struct twl4030_i2c_s *) i2c;
    s->reg = 0x00;
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
 

