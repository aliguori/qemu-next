/*
 *  Exynos4210 General Purpose Input/Output (GPIO) Emulation
 *
 *  Copyright (C) 2012 Samsung Electronics Co Ltd.
 *    Maksim Kozlov, <m.kozlov@samsung.com>
 *    Igor Mitsyanko, <i.mitsyanko@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu-common.h"
#include "sysbus.h"
#include "qdev.h"
#include "irq.h"

/* Debug messages configuration */
#define EXYNOS4210_GPIO_DEBUG             0

#if EXYNOS4210_GPIO_DEBUG == 0
    #define DPRINT_L1(fmt, args...)       do { } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define DPRINT_ERROR(fmt, args...)    do { } while (0)
#elif EXYNOS4210_GPIO_DEBUG == 1
    #define DPRINT_L1(fmt, args...)       \
    do { fprintf(stderr, "QEMU GPIO: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define DPRINT_ERROR(fmt, args...)    \
    do { fprintf(stderr, "QEMU GPIO ERROR: "fmt, ## args); } while (0)
#else
    #define DPRINT_L1(fmt, args...)       \
    do { fprintf(stderr, "QEMU GPIO: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       \
    do { fprintf(stderr, "QEMU GPIO: "fmt, ## args); } while (0)
    #define DPRINT_ERROR(fmt, args...)    \
    do { fprintf(stderr, "QEMU GPIO ERROR: "fmt, ## args); } while (0)
#endif


#define TYPE_EXYNOS4210_GPIO       "exynos4210.gpio"
#define TYPE_EXYNOS4210_GPIO_1_2   "exynos4210.gpio-1_2"
#define TYPE_EXYNOS4210_GPIO_2X    "exynos4210.gpio2x"
#define EXYNOS4210_GPIO(obj)       \
     OBJECT_CHECK(Exynos4GpioState, (obj), TYPE_EXYNOS4210_GPIO)
#define EXYNOS4210_GPIO_1_2(obj)   \
     OBJECT_CHECK(Exynos4Gpio12State, (obj), TYPE_EXYNOS4210_GPIO_1_2)
#define EXYNOS4210_GPIO2X(obj)     \
     OBJECT_CHECK(Exynos4Gpio2XState, (obj), TYPE_EXYNOS4210_GPIO_2X)

#define GPIO1_REGS_MEM_SIZE  0x0b54
#define GPIO2_REGS_MEM_SIZE  0x0b38
#define GPIO2X_REGS_MEM_SIZE 0x0350
#define GPIO3_REGS_MEM_SIZE  0x0018

/* Port group registers offsets */
#define GPIOCON              0x0000 /* Port Group Configuration Register */
#define GPIODAT              0x0004 /* Port Group Data Register */
#define GPIOPUD              0x0008 /* Port Group Pull-up/down Register */
#define GPIODRV              0x000C /* Port Group Drive Strength Ctrl Reg */
#define GPIOCONPDN           0x0010 /* Port Pwr Down Mode Config Reg */
#define GPIOPUDPDN           0x0014 /* Port Pwr Down Mode Pullup/down Reg */

/* GPIO pin functions */
#define GPIOCON_IN           0x0    /* input pin */
#define GPIOCON_OUT          0x1    /* output pin */
#define GPIOCON_EXTINT       0xf    /* external interrupt pin */

/* External interrupts signaling methods */
#define GPIO_INTCON_LOW            0x0    /* interrupt on low level */
#define GPIO_INTCON_HIGH           0x1    /* interrupt on high level */
#define GPIO_INTCON_FALL           0x2    /* interrupt on falling edge */
#define GPIO_INTCON_RISE           0x3    /* interrupt on rising edge */
#define GPIO_INTCON_FALLRISE       0x4    /* interrupt on both edges */

/*
 * Code assumes that each GPIO port group (GPA0, GPA1, e.t.c.)
 * has 8 pins. When calculating GPIO line number to pass to function
 * qdev_get_gpio_in() or qdev_connect_gpio_out(), you must assume the same,
 * even though it's not true for real hardware.
 */
#define GPIO_MAX_PINS_NR       8
#define GPIO_PULLUP_STATE          0x3
#define GPIO_PORTGR_SIZE           0x20
#define DIV_BY_PORTGR_SIZE(x)      ((x) >> 5)
#define MOD_PORTGR_SIZE(x)         ((x) & (GPIO_PORTGR_SIZE - 1))
#define GPIO_EXTINT_SERVICE        0x0B08
#define GPIO_EXTINT_SERVICE_PEND   0x0B0C
#define GPIO_EXTINT_GRPFIXPRI      0x0B10

/* GPIO part 1 specific defines */
#define GPIO1_NORM_PORT_NUM        16
#define GPIO1_ETC_PORT_NUM         2
#define GPIO1_NUM_OF_PORTS         (GPIO1_NORM_PORT_NUM + GPIO1_ETC_PORT_NUM)
#define GPIO1_PORTINT_NUM          GPIO1_NORM_PORT_NUM
#define GPIO1_NORM_PORT_START      0x0
#define GPIO1_NORM_PORT_END        \
    (GPIO1_NORM_PORT_START + GPIO1_NORM_PORT_NUM * GPIO_PORTGR_SIZE - 1)
#define GPIO1_ETCPORT_START        0x0200
#define GPIO1_ETCPORT_END          \
    (GPIO1_ETCPORT_START + GPIO1_ETC_PORT_NUM * GPIO_PORTGR_SIZE - 1)
#define GPIO1_EXTINT_CON_START     0x0700
#define GPIO1_EXTINT_CON_END       0x073C
#define GPIO1_EXTINT_FLT_START     0x0800
#define GPIO1_EXTINT_FLT_END       0x087C
#define GPIO1_EXTINT_MASK_START    0x0900
#define GPIO1_EXTINT_MASK_END      0x093C
#define GPIO1_EXTINT_PEND_START    0x0A00
#define GPIO1_EXTINT_PEND_END      0x0A3C
#define GPIO1_EXTINT_FIXPRI_START  0x0B14
#define GPIO1_EXTINT_FIXPRI_END    0x0B50

/* GPIO part 2 specific defines */
#define GPIO2_NORM_PORT_NUM        16
#define GPIO2_ETC_PORT_NUM         1
#define GPIO2_NUM_OF_PORTS         (GPIO2_NORM_PORT_NUM + GPIO2_ETC_PORT_NUM)
#define GPIO2_PORTINT_NUM          9
#define GPIO2_NORM_PORT_START      0x0
#define GPIO2_NORM_PORT_END        \
    (GPIO2_NORM_PORT_START + GPIO2_NORM_PORT_NUM * GPIO_PORTGR_SIZE - 1)
#define GPIO2_ETCPORT_START        0x0220
#define GPIO2_ETCPORT_END          \
    (GPIO2_ETCPORT_START + GPIO2_ETC_PORT_NUM * GPIO_PORTGR_SIZE - 1)
#define GPIO2_EXTINT_CON_START     0x0700
#define GPIO2_EXTINT_CON_END       0x0724
#define GPIO2_EXTINT_FLT_START     0x0800
#define GPIO2_EXTINT_FLT_END       0x0848
#define GPIO2_EXTINT_MASK_START    0x0900
#define GPIO2_EXTINT_MASK_END      0x0924
#define GPIO2_EXTINT_PEND_START    0x0A00
#define GPIO2_EXTINT_PEND_END      0x0A24
#define GPIO2_EXTINT_FIXPRI_START  0x0B14
#define GPIO2_EXTINT_FIXPRI_END    0x0B38

/* GPIO part2 XPORT specific defines
 * In Exynos documentation X ports are a part of GPIO part2, but we separate
 * them to simplify implementation */
#define GPIO2X_PORT_NUM            4
#define GPIO2X_PORTINT_NUM         GPIO2X_PORT_NUM
#define GPIO2X_PORT_IRQ_NUM        17
#define GPIO2X_PORTS_START         0x0
#define GPIO2X_PORTS_END           \
        (GPIO2X_PORTS_START + GPIO2X_PORT_NUM * GPIO_PORTGR_SIZE - 1)
#define GPIO2X_EXTINT_CON_START    0x0200
#define GPIO2X_EXTINT_CON_END      0x0210
#define GPIO2X_EXTINT_FLT_START    0x0280
#define GPIO2X_EXTINT_FLT_END      0x02A0
#define GPIO2X_EXTINT_MASK_START   0x0300
#define GPIO2X_EXTINT_MASK_END     0x0310
#define GPIO2X_EXTINT_PEND_START   0x0340
#define GPIO2X_EXTINT_PEND_END     0x0350

/* GPIO part 3 specific defines */
#define GPIO3_NUM_OF_PORTS         1
#define GPIO3_NORM_PORT_END        0x0020

typedef enum {
    GPIO_PART2X = 0,
    GPIO_PART1,
    GPIO_PART2,
    GPIO_PART3,
} Exynos4210GpioPart;

typedef struct Exynos4PortGroup {
    uint32_t con;                /* configuration register */
    uint32_t dat;                /* data register */
    uint32_t pud;                /* pull-up/down register */
    uint32_t drv;                /* drive strength control register */
    uint32_t conpdn;             /* configuration register in power down mode */
    uint32_t pudpdn;             /* pull-up/down register in power down mode */

    const char *name;            /* port specific name */
    const uint32_t def_con;      /* default value for configuration register */
    const uint32_t def_pud;      /* default value for pull-up/down register */
    const uint32_t def_drv;      /* default value for drive strength control */
} Exynos4PortGroup;

static const VMStateDescription exynos4_gpio_port_vmstate = {
    .name = "exynos4210.gpio-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(con, Exynos4PortGroup),
        VMSTATE_UINT32(dat, Exynos4PortGroup),
        VMSTATE_UINT32(pud, Exynos4PortGroup),
        VMSTATE_UINT32(drv, Exynos4PortGroup),
        VMSTATE_UINT32(conpdn, Exynos4PortGroup),
        VMSTATE_UINT32(pudpdn, Exynos4PortGroup),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct Exynos4PortIntState {
    uint32_t con;                /* configuration register */
    uint32_t fltcon[2];          /* filter configuraton registers 1,2 */
    uint32_t mask;               /* mask register */
    uint32_t pend;               /* interrupt pending register */
    uint32_t fixpri;             /* fixed priority control register */

    const uint32_t def_mask;     /* default value for mask register */
    const uint8_t int_line_num;  /* external interrupt line number */
} Exynos4PortIntState;

static const VMStateDescription exynos4_gpio_portint_vmstate = {
    .name = "exynos4210.gpio-int",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(con, Exynos4PortIntState),
        VMSTATE_UINT32(con, Exynos4PortIntState),
        VMSTATE_UINT32_ARRAY(fltcon, Exynos4PortIntState, 2),
        VMSTATE_UINT32(mask, Exynos4PortIntState),
        VMSTATE_UINT32(pend, Exynos4PortIntState),
        VMSTATE_UINT32(fixpri, Exynos4PortIntState),
        VMSTATE_END_OF_LIST()
    }
};

static Exynos4PortGroup gpio1_ports[GPIO1_NUM_OF_PORTS] = {
    { .name = "A0", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "A1", .def_con = 0, .def_pud = 0x0555, .def_drv = 0 },
    { .name = "B",  .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "C0", .def_con = 0, .def_pud = 0x0155, .def_drv = 0 },
    { .name = "C1", .def_con = 0, .def_pud = 0x0155, .def_drv = 0 },
    { .name = "D0", .def_con = 0, .def_pud = 0x0055, .def_drv = 0 },
    { .name = "D1", .def_con = 0, .def_pud = 0x0055, .def_drv = 0 },
    { .name = "E0", .def_con = 0, .def_pud = 0x0155, .def_drv = 0 },
    { .name = "E1", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "E2", .def_con = 0, .def_pud = 0x0555, .def_drv = 0 },
    { .name = "E3", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "E4", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "F0", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "F1", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "F2", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "F3", .def_con = 0, .def_pud = 0x0555, .def_drv = 0 },
    { .name = "ETC0", .def_con = 0, .def_pud = 0x0400, .def_drv = 0 },
    { .name = "ETC1", .def_con = 0, .def_pud = 0x005, .def_drv = 0 },
};

/* All pins of all port groups of GPIO part1 share single GPIO IRQ line */
static Exynos4PortIntState gpio1_ports_interrupts[GPIO1_PORTINT_NUM] = {
    { .int_line_num = 1, .def_mask = 0x000000FF },
    { .int_line_num = 2, .def_mask = 0x0000003F },
    { .int_line_num = 3, .def_mask = 0x000000FF },
    { .int_line_num = 4, .def_mask = 0x0000001F },
    { .int_line_num = 5, .def_mask = 0x0000001F },
    { .int_line_num = 6, .def_mask = 0x0000000F },
    { .int_line_num = 7, .def_mask = 0x0000000F },
    { .int_line_num = 8, .def_mask = 0x0000001F },
    { .int_line_num = 9, .def_mask = 0x000000FF },
    { .int_line_num = 10, .def_mask = 0x0000003F },
    { .int_line_num = 11, .def_mask = 0x000000FF },
    { .int_line_num = 12, .def_mask = 0x000000FF },
    { .int_line_num = 13, .def_mask = 0x000000FF },
    { .int_line_num = 14, .def_mask = 0x000000FF },
    { .int_line_num = 15, .def_mask = 0x000000FF },
    { .int_line_num = 16, .def_mask = 0x0000003F },
};

static Exynos4PortGroup gpio2_ports[GPIO2_NUM_OF_PORTS] = {
    { .name = "J0", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "J1", .def_con = 0, .def_pud = 0x0155, .def_drv = 0 },
    { .name = "K0", .def_con = 0, .def_pud = 0x1555, .def_drv = 0x002AAA },
    { .name = "K1", .def_con = 0, .def_pud = 0x1555, .def_drv = 0 },
    { .name = "K2", .def_con = 0, .def_pud = 0x1555, .def_drv = 0 },
    { .name = "K3", .def_con = 0, .def_pud = 0x1555, .def_drv = 0 },
    { .name = "L0", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "L1", .def_con = 0, .def_pud = 0x0015, .def_drv = 0 },
    { .name = "L2", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "Y0", .def_con = 0x00225522, .def_pud = 0, .def_drv = 0x000AAA },
    { .name = "Y1", .def_con = 0x00002222, .def_pud = 0, .def_drv = 0x0000AA },
    { .name = "Y2", .def_con = 0x00255555, .def_pud = 0, .def_drv = 0x000AAA },
    { .name = "Y3", .def_con = 0x22222222, .def_pud = 0, .def_drv = 0x00AAAA },
    { .name = "Y4", .def_con = 0x22222222, .def_pud = 0, .def_drv = 0x00AAAA },
    { .name = "Y5", .def_con = 0x22222222, .def_pud = 0, .def_drv = 0x00AAAA },
    { .name = "Y6", .def_con = 0x22222222, .def_pud = 0, .def_drv = 0x00AAAA },
    { .name = "ETC6", .def_con = 0, .def_pud = 0xC0C0, .def_drv = 0 },
};

/* All pins of all port groups of GPIO part2 share single interrupt line */
static Exynos4PortIntState gpio2_ports_interrupts[GPIO2_PORTINT_NUM] = {
    { .int_line_num = 21, .def_mask = 0x000000FF },
    { .int_line_num = 22, .def_mask = 0x0000001F },
    { .int_line_num = 23, .def_mask = 0x0000007F },
    { .int_line_num = 24, .def_mask = 0x0000007F },
    { .int_line_num = 25, .def_mask = 0x0000007F },
    { .int_line_num = 26, .def_mask = 0x0000007F },
    { .int_line_num = 27, .def_mask = 0x000000FF },
    { .int_line_num = 28, .def_mask = 0x00000007 },
    { .int_line_num = 29, .def_mask = 0x000000FF },
};

static Exynos4PortGroup gpio2x_ports[GPIO2X_PORT_NUM] = {
    { .name = "X0", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "X1", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "X2", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
    { .name = "X3", .def_con = 0, .def_pud = 0x5555, .def_drv = 0 },
};

/* Ports X0 and X1 have separate external irq lines for every pin.
 * All pins of ports X2 and X3 share single external irq line */
static Exynos4PortIntState gpio2x_ports_interrupts[GPIO2X_PORTINT_NUM] = {
    { .int_line_num = 0, .def_mask = 0x000000FF },
    { .int_line_num = 1, .def_mask = 0x000000FF },
    { .int_line_num = 2, .def_mask = 0x000000FF },
    { .int_line_num = 3, .def_mask = 0x000000FF },
};

static Exynos4PortGroup gpio3_ports = {
    .name = "Z", .def_con = 0, .def_pud = 0x1555, .def_drv = 0
};

typedef struct Exynos4GpioState {
    SysBusDevice busdev;
    MemoryRegion iomem;
    Exynos4210GpioPart part;
    qemu_irq *out_cb;             /* Callbacks on writing to GPIO line */
    Exynos4PortGroup *ports;
    Exynos4PortIntState *port_ints;
} Exynos4GpioState;

typedef struct Exynos4Gpio12State {
    Exynos4GpioState gpio_common;
    qemu_irq irq_gpio;        /* GPIO interrupt request */
    /* Group index and pin # of highest priority currently pended irq line */
    uint32_t extint_serv;
    uint32_t extint_serv_pend;
    /* Index of highest priority interrupt group */
    uint32_t extint_grpfixpri;
} Exynos4Gpio12State;

typedef struct Exynos4Gpio2XState {
    Exynos4GpioState gpio_common;
    qemu_irq ext_irq[GPIO2X_PORT_IRQ_NUM];
} Exynos4Gpio2XState;

static inline void exynos4_gpio_reset_portgr(Exynos4PortGroup *group)
{
    group->con = group->def_con;
    group->dat = 0;
    group->pud = group->def_pud;
    group->drv = group->def_drv;
    group->conpdn = 0;
    group->pudpdn = 0;
}

static inline void exynos4_gpio_reset_portint(Exynos4PortIntState *pint)
{
    pint->con = 0;
    pint->mask = pint->def_mask;
    pint->fltcon[0] = 0;
    pint->fltcon[1] = 0;
    pint->pend = 0;
    pint->fixpri = 0;
}

static uint32_t
exynos4_gpio_portgr_read(Exynos4PortGroup *group, target_phys_addr_t ofst)
{
    DPRINT_L2("Port group GP%s read offset 0x%x\n", group->name, ofst);

    switch (ofst) {
    case GPIOCON:
        return group->con;
    case GPIODAT:
        return group->dat & 0xff;
    case GPIOPUD:
        return group->pud;
    case GPIODRV:
        return group->drv;
    case GPIOCONPDN:
        return group->conpdn;
    case GPIOPUDPDN:
        return group->pudpdn;
    default:
        DPRINT_ERROR("Port group GP%s bad read 0x%x\n", group->name, ofst);
        return 0xBAADBAAD;
    }
}

static uint32_t
exynos4_etc_portgroup_read(Exynos4PortGroup *group, target_phys_addr_t ofst)
{
    DPRINT_L1("Port group %s read offset 0x%x\n", group->name, ofst);

    switch (ofst) {
    case GPIOPUD:
        return group->pud;
    case GPIODRV:
        return group->drv;
    default:
        DPRINT_ERROR("Port group %s bad read offset 0x%x\n", group->name, ofst);
        return 0xBAADBAAD;
    }
}

static void exynos4_etc_portgroup_write(Exynos4PortGroup *group,
        target_phys_addr_t ofst, uint32_t value)
{
    DPRINT_L1("Port group %s write: offset 0x%x = %u(0x%x)\n",
            group->name, ofst, value, value);

    switch (ofst) {
    case GPIOPUD:
        group->pud = value;
        break;
    case GPIODRV:
        group->drv = value;
        break;
    default:
        DPRINT_ERROR("Port group %s bad write: offset 0x%x = %u(0x%x)\n",
            group->name, ofst, value, value);
        break;
    }
}

/* Returns index of currently pended external interrupt line with highest
 * priority 1..MAX_INDEX for GPIO parts 1 and 2 */
static unsigned int
gpio_group_get_highest_prio(Exynos4GpioState *g)
{
    Exynos4Gpio12State *g12 = EXYNOS4210_GPIO_1_2(g);
    unsigned i;
    const unsigned num_of_prio = g->part == GPIO_PART1 ? GPIO1_PORTINT_NUM :
            GPIO2_PORTINT_NUM;
    uint8_t highest_prio = g12->extint_grpfixpri;

    if (highest_prio == 0) {
        /* Zero extint_grpfixpri is equal to extint_grpfixpri == 1 */
        highest_prio = 1;
    }

    /* Corresponding group index is less then EXTINT index by one */
    highest_prio--;
    for (i = 0; i < num_of_prio; i++) {
        if (g->port_ints[highest_prio].pend &
                ~g->port_ints[highest_prio].mask) {
            return highest_prio + 1;
        }
        if (++highest_prio >= num_of_prio) {
            highest_prio = 0;
        }
    }

    return 0;
}

/* Returns line number of highest pended external irq within portgroup */
static unsigned int gpio_get_highest_intnum(Exynos4GpioState *g, unsigned group)
{
    uint8_t highest_prio = g->port_ints[group].fixpri;
    uint8_t pend = g->port_ints[group].pend;
    unsigned i;

    for (i = 0; i < GPIO_MAX_PINS_NR; i++) {
        if (pend & (1 << highest_prio)) {
            return highest_prio;
        }
        if (++highest_prio >= GPIO_MAX_PINS_NR) {
            highest_prio = 0;
        }
    }

    return 0;
}

/* Clear GPIO IRQ if none of gpio interrupt lines are pended */
static void exynos4_gpioirq_update(Exynos4GpioState *g)
{
    Exynos4Gpio12State *g12 = EXYNOS4210_GPIO_1_2(g);
    unsigned pend_prio = gpio_group_get_highest_prio(g);

    if (pend_prio == 0) {
        DPRINT_L2("GPIO part %u interrupt cleared\n", g->part);
        g12->extint_serv = g12->extint_serv_pend = 0;
        qemu_irq_lower(g12->irq_gpio);
    } else if (pend_prio != ((g12->extint_serv >> 3) & 0x1f)) {
        g12->extint_serv = (pend_prio << 3) |
                gpio_get_highest_intnum(g, pend_prio - 1);
        g12->extint_serv_pend = g->port_ints[pend_prio - 1].pend;
    }
}

static void exynos4_gpio_portgr_write(Exynos4GpioState *g, int idx,
        unsigned int ofst, uint32_t value)
{
    Exynos4PortGroup *group = &g->ports[idx];
    unsigned pin;
    uint32_t diff, old_con, new_dat;

    DPRINT_L1("Port group GP%s write: ofst 0x%x = %u(0x%x)\n",
            group->name, ofst, value, value);

    switch (ofst) {
    case GPIOCON:
        old_con = group->con;
        group->con = value;
        for (pin = 0; pin < GPIO_MAX_PINS_NR; pin++) {
            if (((value >> pin * 4) & 0xf) != ((old_con >> pin * 4) & 0xf) &&
                    ((value >> pin * 4) & 0xf) == 0x1) {
                qemu_set_irq(g->out_cb[idx * GPIO_MAX_PINS_NR + pin],
                        !!(group->dat & (1 << pin)));
            }
        }
        break;
    case GPIODAT:
        new_dat = group->dat;
        value &= (1 << GPIO_MAX_PINS_NR) - 1;
        for (pin = 0; pin < GPIO_MAX_PINS_NR; pin++) {
            if (((group->con >> pin * 4) & 0xf) == GPIOCON_OUT) {
                new_dat = (new_dat & ~(1 << pin)) | (value & (1 << pin));
            }
        }
        diff = group->dat ^ new_dat;
        group->dat = new_dat & ((1 << GPIO_MAX_PINS_NR) - 1);
        while ((pin = ffs(diff))) {
            pin--;
            DPRINT_L2("Port group GP%s pin #%u write callback %s raised\n",
                      group->name, pin, (g->out_cb[idx *
                      GPIO_MAX_PINS_NR + pin] ? "" : "wasn't"));
            qemu_set_irq(g->out_cb[idx * GPIO_MAX_PINS_NR + pin],
                    !!(group->dat & (1 << pin)));
            diff &= ~(1 << pin);
        }
        break;
    case GPIOPUD:
        for (pin = 0; pin < GPIO_MAX_PINS_NR; pin++) {
            if (((value >> 2 * pin) & 0x3) == GPIO_PULLUP_STATE &&
                    ((group->pud >> 2 * pin) & 0x3) != GPIO_PULLUP_STATE) {
                group->dat |= 1 << pin;
            }
        }
        group->pud = value;
        break;
    case GPIODRV:
        group->drv = value;
        break;
    case GPIOCONPDN:
        group->conpdn = value;
        break;
    case GPIOPUDPDN:
        group->pudpdn = value;
        break;
    default:
        DPRINT_ERROR("Port group GP%s bad write: offset 0x%x = %u(0x%x)\n",
            group->name, ofst, value, value);
        break;
    }
}

static void exynos4_gpio_set_cb(void *opaque, int line, int level)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    const unsigned group_num = line >> 3;
    const unsigned pin = line & (GPIO_MAX_PINS_NR - 1);
    bool irq_is_triggered = false;
    const uint32_t dat_prev = g->ports[group_num].dat & (1 << pin);
    const unsigned pin_func = (g->ports[group_num].con >> pin * 4) & 0xf;

    /* Check that corresponding pin is in input state */
    if (pin_func != GPIOCON_EXTINT && pin_func != GPIOCON_IN) {
        return;
    }

    DPRINT_L1("Input pin GPIO%s_PIN%u %s by external device\n",
            g->ports[group_num].name, pin, (level ? "set" : "cleared"));
    /* Set new value on corresponding gpio pin */
    if (level) {
        g->ports[group_num].dat |= (1 << pin);
    } else {
        g->ports[group_num].dat &= ~(1 << pin);
    }

    /* Check that external interrupt function is active for this pin */
    if (pin_func != GPIOCON_EXTINT) {
        return;
    }

    /* Do nothing if corresponding interrupt line is masked or already pended */
    if ((g->port_ints[group_num].mask & (1 << pin)) ||
            (g->port_ints[group_num].pend & (1 << pin))) {
        return;
    }

    /* Get interrupt line signaling method */
    switch ((g->port_ints[group_num].con >> (pin * 4)) & 7) {
    case GPIO_INTCON_LOW:
        irq_is_triggered = !level;
        break;
    case GPIO_INTCON_HIGH:
        irq_is_triggered = !!level;
        break;
    case GPIO_INTCON_FALL:
        irq_is_triggered = dat_prev && !(g->ports[group_num].dat & (1 << pin));
        break;
    case GPIO_INTCON_RISE:
        irq_is_triggered = !dat_prev && (g->ports[group_num].dat & (1 << pin));
        break;
    case GPIO_INTCON_FALLRISE:
        irq_is_triggered =
            (dat_prev && !(g->ports[group_num].dat & (1 << pin))) ||
            (!dat_prev && (g->ports[group_num].dat & (1 << pin)));
        break;
    default:
        DPRINT_ERROR("GPIO PART%u: unknown triggering method of EXT_IRQ_%u\n",
                g->part, g->port_ints[group_num].int_line_num);
        break;
    }

    if (irq_is_triggered) {
        g->port_ints[group_num].pend |= 1 << pin;
        if (g->part == GPIO_PART2X) {
            unsigned irq = group_num * GPIO_MAX_PINS_NR + pin;
            DPRINT_L1("IRQ_EINT%u raised\n", irq);
            if (irq >= GPIO2X_PORT_IRQ_NUM) {
                irq = GPIO2X_PORT_IRQ_NUM - 1;
            }
            qemu_irq_raise(EXYNOS4210_GPIO2X(g)->ext_irq[irq]);
        } else {
            Exynos4Gpio12State *g12 = EXYNOS4210_GPIO_1_2(g);
            DPRINT_L1("GPIO_INT%u[PIN%u] raised and GPIO_P%u_IRQ raised\n",
                    g->port_ints[group_num].int_line_num, pin, g->part);

            if ((group_num + 1) == gpio_group_get_highest_prio(g)) {
                g12->extint_serv = ((group_num + 1) << 3) |
                        gpio_get_highest_intnum(g, group_num);
                g12->extint_serv_pend = g->port_ints[group_num].pend;
            }
            qemu_irq_raise(g12->irq_gpio);
        }
    }
}

static uint64_t
exynos4_gpio1_readfn(void *opaque, target_phys_addr_t ofst, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx;
    DPRINT_L2("GPIO1 read offset 0x%x\n", (uint32_t)ofst);

    switch (ofst) {
    case GPIO1_NORM_PORT_START ... GPIO1_NORM_PORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        return exynos4_gpio_portgr_read(&g->ports[idx], MOD_PORTGR_SIZE(ofst));
    case GPIO1_EXTINT_MASK_START ... GPIO1_EXTINT_MASK_END:
        idx = (ofst - GPIO1_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_MASK register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].mask;
    case GPIO1_EXTINT_PEND_START ... GPIO1_EXTINT_PEND_END:
        idx = (ofst - GPIO1_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_PEND register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].pend;
    case GPIO1_EXTINT_CON_START ... GPIO1_EXTINT_CON_END:
        idx = (ofst - GPIO1_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_CON register read\n",
            g->port_ints[idx].int_line_num);
        return g->port_ints[idx].con;
    case GPIO1_EXTINT_FLT_START ... GPIO1_EXTINT_FLT_END:
        idx = ((ofst - GPIO1_EXTINT_FLT_START) >> 2) & 1;
        DPRINT_L1("GPIO1 EXTINT%u_FLTCON%u register read\n",
          g->port_ints[(ofst - GPIO1_EXTINT_FLT_START) >> 3].int_line_num, idx);
        return g->port_ints[(ofst - GPIO1_EXTINT_FLT_START) >> 3].fltcon[idx];
    case GPIO_EXTINT_SERVICE:
        DPRINT_L1("GPIO1 EXT_INT_SERVICE_XA read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_serv;
    case GPIO_EXTINT_SERVICE_PEND:
        DPRINT_L1("GPIO1 EXT_INT_SERVICE_PEND_XA read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_serv_pend;
    case GPIO_EXTINT_GRPFIXPRI:
        DPRINT_L1("GPIO1 EXT_INT_GRPFIXPRI read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_grpfixpri;
    case GPIO1_EXTINT_FIXPRI_START ... GPIO1_EXTINT_FIXPRI_END:
        idx = (ofst - GPIO1_EXTINT_FIXPRI_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_FIXPRI register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].fixpri;
    case GPIO1_ETCPORT_START ... GPIO1_ETCPORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst - GPIO1_ETCPORT_START) +
            GPIO1_NORM_PORT_NUM;
        return exynos4_etc_portgroup_read(&g->ports[idx],
                MOD_PORTGR_SIZE(ofst - GPIO1_ETCPORT_START));
    default:
        DPRINT_ERROR("GPIO1 bad read offset 0x%x\n", (uint32_t)ofst);
        return 0;
    }
}

static uint64_t
exynos4_gpio2_readfn(void *opaque, target_phys_addr_t ofst, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx;
    DPRINT_L2("GPIO2 read offset 0x%x\n", (uint32_t)ofst);

    switch (ofst) {
    case GPIO2_NORM_PORT_START ... GPIO2_NORM_PORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        return exynos4_gpio_portgr_read(&g->ports[idx], MOD_PORTGR_SIZE(ofst));
    case GPIO2_EXTINT_MASK_START ... GPIO2_EXTINT_MASK_END:
        idx = (ofst - GPIO2_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_MASK register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].mask;
    case GPIO2_EXTINT_PEND_START ... GPIO2_EXTINT_PEND_END:
        idx = (ofst - GPIO2_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_PEND register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].pend;
    case GPIO2_EXTINT_CON_START ... GPIO2_EXTINT_CON_END:
        idx = (ofst - GPIO2_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_CON register read\n",
            g->port_ints[idx].int_line_num);
        return g->port_ints[idx].con;
    case GPIO2_EXTINT_FLT_START ... GPIO2_EXTINT_FLT_END:
        idx = ((ofst - GPIO2_EXTINT_FLT_START) >> 2) & 1;
        DPRINT_L1("GPIO2 EXTINT%u_FLTCON%u register read\n",
          g->port_ints[(ofst - GPIO2_EXTINT_FLT_START) >> 3].int_line_num, idx);
        return g->port_ints[(ofst - GPIO2_EXTINT_FLT_START) >> 3].fltcon[idx];
    case GPIO_EXTINT_SERVICE:
        DPRINT_L1("GPIO2 EXT_INT_SERVICE_XA read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_serv;
    case GPIO_EXTINT_SERVICE_PEND:
        DPRINT_L1("GPIO2 EXT_INT_SERVICE_PEND_XA read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_serv_pend;
    case GPIO_EXTINT_GRPFIXPRI:
        DPRINT_L1("GPIO2 EXT_INT_GRPFIXPRI read\n");
        return EXYNOS4210_GPIO_1_2(g)->extint_grpfixpri;
    case GPIO2_EXTINT_FIXPRI_START ... GPIO2_EXTINT_FIXPRI_END:
        idx = (ofst - GPIO2_EXTINT_FIXPRI_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_FIXPRI register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].fixpri;
    case GPIO2_ETCPORT_START ... GPIO2_ETCPORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst - GPIO2_ETCPORT_START) +
            GPIO2_NORM_PORT_NUM;
        return exynos4_etc_portgroup_read(&g->ports[idx],
                MOD_PORTGR_SIZE(ofst - GPIO2_ETCPORT_START));
    default:
        DPRINT_ERROR("GPIO2 bad read offset 0x%x\n", (uint32_t)ofst);
        return 0;
    }
}

static uint64_t
exynos4_gpio2x_readfn(void *opaque, target_phys_addr_t ofst, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx;
    DPRINT_L2("GPIO2x read offset 0x%x\n", (uint32_t)ofst);

    switch (ofst) {
    case GPIO2X_PORTS_START ... GPIO2X_PORTS_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        return exynos4_gpio_portgr_read(&g->ports[idx], MOD_PORTGR_SIZE(ofst));
    case GPIO2X_EXTINT_MASK_START ... GPIO2X_EXTINT_MASK_END:
        idx = (ofst - GPIO2X_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_MASK register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].mask;
    case GPIO2X_EXTINT_PEND_START ... GPIO2X_EXTINT_PEND_END:
        idx = (ofst - GPIO2X_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_PEND register read\n",
                g->port_ints[idx].int_line_num);
        return g->port_ints[idx].pend;
    case GPIO2X_EXTINT_CON_START ... GPIO2X_EXTINT_CON_END:
        idx = (ofst - GPIO2X_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_CON register read\n",
            g->port_ints[idx].int_line_num);
        return g->port_ints[idx].con;
    case GPIO2X_EXTINT_FLT_START ... GPIO2X_EXTINT_FLT_END:
        idx = ((ofst - GPIO2X_EXTINT_FLT_START) >> 2) & 1;
        DPRINT_L1("GPIO2X EXTINT%u_FLTCON%u register read\n",
         g->port_ints[(ofst - GPIO2X_EXTINT_FLT_START) >> 3].int_line_num, idx);
        return g->port_ints[(ofst - GPIO2X_EXTINT_FLT_START) >> 3].fltcon[idx];
    default:
        DPRINT_ERROR("GPIO2X bad read offset 0x%x\n", (uint32_t)ofst);
        return 0;
    }
}

static uint64_t
exynos4_gpio3_readfn(void *opaque, target_phys_addr_t ofst, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    DPRINT_L2("GPIO3 read offset 0x%x\n", (uint32_t)ofst);

    if (ofst < GPIO3_NORM_PORT_END) {
        return exynos4_gpio_portgr_read(&g->ports[DIV_BY_PORTGR_SIZE(ofst)],
                MOD_PORTGR_SIZE(ofst));
    }

    DPRINT_ERROR("GPIO3 bad read offset 0x%x\n", (uint32_t)ofst);
    return 0;
}

static void exynos4_gpio1_writefn(void *opaque, target_phys_addr_t ofst,
                               uint64_t value, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx;
    DPRINT_L2("GPIO1 write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);

    switch (ofst) {
    case GPIO1_NORM_PORT_START ... GPIO1_NORM_PORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        exynos4_gpio_portgr_write(g, idx, MOD_PORTGR_SIZE(ofst), value);
        break;
    case GPIO1_EXTINT_MASK_START ... GPIO1_EXTINT_MASK_END:
        idx = (ofst - GPIO1_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_MASK register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].mask = value;
        break;
    case GPIO1_EXTINT_PEND_START ... GPIO1_EXTINT_PEND_END:
        idx = (ofst - GPIO1_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_PEND register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].pend &= ~value;
        exynos4_gpioirq_update(g);
        break;
    case GPIO1_EXTINT_CON_START ... GPIO1_EXTINT_CON_END:
        idx = (ofst - GPIO1_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_CON register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].con = value;
        break;
    case GPIO1_EXTINT_FLT_START ... GPIO1_EXTINT_FLT_END:
        idx = ((ofst - GPIO1_EXTINT_FLT_START) >> 2) & 1;
        DPRINT_L1("GPIO1 EXTINT%u_FLTCON%u reg write = %u(0x%x)\n",
           g->port_ints[(ofst - GPIO1_EXTINT_FLT_START) >> 3].int_line_num,
           idx, (uint32_t)value, (uint32_t)value);
        g->port_ints[(ofst - GPIO1_EXTINT_FLT_START) >> 3].fltcon[idx] = value;
        break;
    case GPIO_EXTINT_SERVICE:
        DPRINT_L1("GPIO1 EXT_INT_SERVICE_XA register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_serv = value;
        break;
    case GPIO_EXTINT_SERVICE_PEND:
        DPRINT_L1("GPIO1 EXT_INT_SERVICE_PEND_XA register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_serv_pend = value;
        break;
    case GPIO_EXTINT_GRPFIXPRI:
        DPRINT_L1("GPIO1 EXT_INT_GRPFIXPRI register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_grpfixpri = value;
        break;
    case GPIO1_EXTINT_FIXPRI_START ... GPIO1_EXTINT_FIXPRI_END:
        idx = (ofst - GPIO1_EXTINT_FIXPRI_START) >> 2;
        DPRINT_L1("GPIO1 EXTINT%u_FIXPRI register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].fixpri = value;
        break;
    case GPIO1_ETCPORT_START ... GPIO1_ETCPORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst - GPIO1_ETCPORT_START) +
            GPIO1_NORM_PORT_NUM;
        exynos4_etc_portgroup_write(&g->ports[idx],
            MOD_PORTGR_SIZE(ofst - GPIO1_ETCPORT_START), value);
        break;
    default:
        DPRINT_ERROR("GPIO1 bad write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);
        break;
    }
}

static void exynos4_gpio2_writefn(void *opaque, target_phys_addr_t ofst,
                               uint64_t value, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx;
    DPRINT_L2("GPIO2 write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);

    switch (ofst) {
    case GPIO2_NORM_PORT_START ... GPIO2_NORM_PORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        exynos4_gpio_portgr_write(g, idx, MOD_PORTGR_SIZE(ofst), value);
        break;
    case GPIO2_EXTINT_MASK_START ... GPIO2_EXTINT_MASK_END:
        idx = (ofst - GPIO2_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_MASK register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].mask = value;
        break;
    case GPIO2_EXTINT_PEND_START ... GPIO2_EXTINT_PEND_END:
        idx = (ofst - GPIO2_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_PEND register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].pend &= ~value;
        exynos4_gpioirq_update(g);
        break;
    case GPIO2_EXTINT_CON_START ... GPIO2_EXTINT_CON_END:
        idx = (ofst - GPIO2_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_CON register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].con = value;
        break;
    case GPIO2_EXTINT_FLT_START ... GPIO2_EXTINT_FLT_END:
        idx = ((ofst - GPIO2_EXTINT_FLT_START) >> 2) & 1;
        DPRINT_L1("GPIO2 EXTINT%u_FLTCON%u reg write = %u(0x%x)\n",
           g->port_ints[(ofst - GPIO2_EXTINT_FLT_START) >> 3].int_line_num,
           idx, (uint32_t)value, (uint32_t)value);
        g->port_ints[(ofst - GPIO2_EXTINT_FLT_START) >> 3].fltcon[idx] = value;
        break;
    case GPIO_EXTINT_SERVICE:
        DPRINT_L1("GPIO2 EXT_INT_SERVICE_XA register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_serv = value;
        break;
    case GPIO_EXTINT_SERVICE_PEND:
        DPRINT_L1("GPIO2 EXT_INT_SERVICE_PEND_XA register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_serv_pend = value;
        break;
    case GPIO_EXTINT_GRPFIXPRI:
        DPRINT_L1("GPIO2 EXT_INT_GRPFIXPRI register write = %u(0x%x)\n",
                (uint32_t)value, (uint32_t)value);
        EXYNOS4210_GPIO_1_2(g)->extint_grpfixpri = value;
        break;
    case GPIO2_EXTINT_FIXPRI_START ... GPIO2_EXTINT_FIXPRI_END:
        idx = (ofst - GPIO2_EXTINT_FIXPRI_START) >> 2;
        DPRINT_L1("GPIO2 EXTINT%u_FIXPRI register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].fixpri = value;
        break;
    case GPIO2_ETCPORT_START ... GPIO2_ETCPORT_END:
        idx = DIV_BY_PORTGR_SIZE(ofst - GPIO2_ETCPORT_START) +
            GPIO2_NORM_PORT_NUM;
        exynos4_etc_portgroup_write(&g->ports[idx],
            MOD_PORTGR_SIZE(ofst - GPIO2_ETCPORT_START), value);
        break;
    default:
        DPRINT_ERROR("GPIO2 bad write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);
        break;
    }
}

static void exynos4_gpio2x_writefn(void *opaque, target_phys_addr_t ofst,
                               uint64_t value, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    unsigned idx, i;
    Exynos4Gpio2XState *g2;
    DPRINT_L2("GPIO2X write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);

    switch (ofst) {
    case GPIO2X_PORTS_START ... GPIO2X_PORTS_END:
        idx = DIV_BY_PORTGR_SIZE(ofst);
        exynos4_gpio_portgr_write(g, idx, MOD_PORTGR_SIZE(ofst), value);
        break;
    case GPIO2X_EXTINT_MASK_START ... GPIO2X_EXTINT_MASK_END:
        idx = (ofst - GPIO2X_EXTINT_MASK_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_MASK register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].mask = value;
        break;
    case GPIO2X_EXTINT_PEND_START ... GPIO2X_EXTINT_PEND_END:
        idx = (ofst - GPIO2X_EXTINT_PEND_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_PEND register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g2 = EXYNOS4210_GPIO2X(g);
        for (i = 0; i < GPIO_MAX_PINS_NR; i++) {
            if ((g->port_ints[idx].pend & (1 << i)) && (value & (1 << i))) {
                unsigned irq_n = idx * GPIO_MAX_PINS_NR + i;
                g->port_ints[idx].pend &= ~(1 << i);
                if (irq_n >= GPIO2X_PORT_IRQ_NUM) {
                    irq_n = GPIO2X_PORT_IRQ_NUM - 1;
                }
                DPRINT_L1("GPIO2X EINT%u lowered\n", irq_n);
                qemu_irq_lower(g2->ext_irq[irq_n]);
            }
        }
        break;
    case GPIO2X_EXTINT_CON_START ... GPIO2X_EXTINT_CON_END:
        idx = (ofst - GPIO2X_EXTINT_CON_START) >> 2;
        DPRINT_L1("GPIO2X EXTINT%u_CON register write = %u(0x%x)\n",
            g->port_ints[idx].int_line_num, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].con = value;
        break;
    case GPIO2X_EXTINT_FLT_START ... GPIO2X_EXTINT_FLT_END:
        i = ((ofst - GPIO2X_EXTINT_FLT_START) >> 2) & 1;
        idx = (ofst - GPIO2X_EXTINT_FLT_START) >> 3;
        DPRINT_L1("GPIO2X EXTINT%u_FLTCON%u reg write = %u(0x%x)\n",
           g->port_ints[idx].int_line_num, i, (uint32_t)value, (uint32_t)value);
        g->port_ints[idx].fltcon[i] = value;
        break;
    default:
        DPRINT_ERROR("GPIO2X bad write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);
        break;
    }
}

static void exynos4_gpio3_writefn(void *opaque, target_phys_addr_t ofst,
                               uint64_t value, unsigned size)
{
    Exynos4GpioState *g = (Exynos4GpioState *)opaque;
    DPRINT_L2("GPIO3 write offset 0x%x = %u(0x%x)\n",
            (uint32_t)ofst, (uint32_t)value, (uint32_t)value);

    if (ofst >= GPIO3_NORM_PORT_END) {
        DPRINT_ERROR("GPIO3 bad write offset 0x%x = %u(0x%x)\n",
                (uint32_t)ofst, (uint32_t)value, (uint32_t)value);
        return;
    }

    exynos4_gpio_portgr_write(g, DIV_BY_PORTGR_SIZE(ofst),
            MOD_PORTGR_SIZE(ofst), value);
}

static const MemoryRegionOps exynos4_gpio1_mmio_ops = {
    .read = exynos4_gpio1_readfn,
    .write = exynos4_gpio1_writefn,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps exynos4_gpio2_mmio_ops = {
    .read = exynos4_gpio2_readfn,
    .write = exynos4_gpio2_writefn,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps exynos4_gpio2x_mmio_ops = {
    .read = exynos4_gpio2x_readfn,
    .write = exynos4_gpio2x_writefn,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps exynos4_gpio3_mmio_ops = {
    .read = exynos4_gpio3_readfn,
    .write = exynos4_gpio3_writefn,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription exynos4_gpio1_vmstate = {
    .name = "exynos4210.gpio1",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY_POINTER(gpio_common.ports, Exynos4Gpio12State,
            GPIO1_NUM_OF_PORTS, exynos4_gpio_port_vmstate, Exynos4PortGroup),
        VMSTATE_STRUCT_ARRAY_POINTER(gpio_common.port_ints, Exynos4Gpio12State,
            GPIO1_PORTINT_NUM, exynos4_gpio_portint_vmstate,
            Exynos4PortIntState),
        VMSTATE_UINT32(extint_serv, Exynos4Gpio12State),
        VMSTATE_UINT32(extint_serv_pend, Exynos4Gpio12State),
        VMSTATE_UINT32(extint_grpfixpri, Exynos4Gpio12State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription exynos4_gpio2_vmstate = {
    .name = "exynos4210.gpio2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY_POINTER(gpio_common.ports, Exynos4Gpio12State,
            GPIO2_NUM_OF_PORTS, exynos4_gpio_port_vmstate, Exynos4PortGroup),
        VMSTATE_STRUCT_ARRAY_POINTER(gpio_common.port_ints, Exynos4Gpio12State,
            GPIO2_PORTINT_NUM, exynos4_gpio_portint_vmstate,
            Exynos4PortIntState),
        VMSTATE_UINT32(extint_serv, Exynos4Gpio12State),
        VMSTATE_UINT32(extint_serv_pend, Exynos4Gpio12State),
        VMSTATE_UINT32(extint_grpfixpri, Exynos4Gpio12State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription exynos4_gpio2x_vmstate = {
    .name = "exynos4210.gpio2x",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY_POINTER(ports, Exynos4GpioState,
            GPIO2X_PORT_NUM, exynos4_gpio_port_vmstate, Exynos4PortGroup),
        VMSTATE_STRUCT_ARRAY_POINTER(port_ints, Exynos4GpioState,
            GPIO2X_PORTINT_NUM, exynos4_gpio_portint_vmstate,
            Exynos4PortIntState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription exynos4_gpio3_vmstate = {
    .name = "exynos4210.gpio3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY_POINTER(gpio_common.ports, Exynos4Gpio12State,
            GPIO3_NUM_OF_PORTS, exynos4_gpio_port_vmstate, Exynos4PortGroup),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4_gpio1_initfn(Object *obj)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(obj);

    g->part = GPIO_PART1;
    g->ports = gpio1_ports;
    g->port_ints = gpio1_ports_interrupts;
    g->out_cb = g_new0(qemu_irq, GPIO1_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
}

static void exynos4_gpio2_initfn(Object *obj)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(obj);

    g->part = GPIO_PART2;
    g->ports = gpio2_ports;
    g->port_ints = gpio2_ports_interrupts;
    g->out_cb = g_new0(qemu_irq, GPIO2_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
}

static void exynos4_gpio2x_initfn(Object *obj)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(obj);

    g->part = GPIO_PART2X;
    g->ports = gpio2x_ports;
    g->port_ints = gpio2x_ports_interrupts;
    g->out_cb = g_new0(qemu_irq, GPIO2X_PORT_NUM * GPIO_MAX_PINS_NR);
}

static void exynos4_gpio3_initfn(Object *obj)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(obj);

    g->part = GPIO_PART3;
    g->ports = &gpio3_ports;
    g->out_cb = g_new0(qemu_irq, GPIO3_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
}

static int exynos4_gpio1_realize(SysBusDevice *busdev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(busdev);

    sysbus_init_irq(busdev, &EXYNOS4210_GPIO_1_2(busdev)->irq_gpio);
    qdev_init_gpio_in(DEVICE(busdev), exynos4_gpio_set_cb,
            GPIO1_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    qdev_init_gpio_out(DEVICE(busdev), g->out_cb,
            GPIO1_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    memory_region_init_io(&g->iomem, &exynos4_gpio1_mmio_ops, g,
            "exynos4210.gpio1", GPIO1_REGS_MEM_SIZE);
    sysbus_init_mmio(busdev, &g->iomem);
    return 0;
}

static int exynos4_gpio2_realize(SysBusDevice *busdev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(busdev);

    sysbus_init_irq(busdev, &EXYNOS4210_GPIO_1_2(busdev)->irq_gpio);
    qdev_init_gpio_in(DEVICE(busdev), exynos4_gpio_set_cb,
            GPIO2_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    qdev_init_gpio_out(DEVICE(busdev), g->out_cb,
            GPIO2_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    memory_region_init_io(&g->iomem, &exynos4_gpio2_mmio_ops, g,
            "exynos4210.gpio2", GPIO2_REGS_MEM_SIZE);
    sysbus_init_mmio(busdev, &g->iomem);
    return 0;
}

static int exynos4_gpio2x_realize(SysBusDevice *busdev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(busdev);
    Exynos4Gpio2XState *g2x = EXYNOS4210_GPIO2X(busdev);
    unsigned i;

    for (i = 0; i < GPIO2X_PORT_IRQ_NUM; i++) {
        sysbus_init_irq(busdev, &g2x->ext_irq[i]);
    }

    qdev_init_gpio_in(DEVICE(busdev), exynos4_gpio_set_cb,
            GPIO2X_PORT_NUM * GPIO_MAX_PINS_NR);
    qdev_init_gpio_out(DEVICE(busdev), g->out_cb,
            GPIO2X_PORT_NUM * GPIO_MAX_PINS_NR);
    memory_region_init_io(&g->iomem, &exynos4_gpio2x_mmio_ops, g,
            "exynos4210.gpio2x", GPIO2X_REGS_MEM_SIZE);
    sysbus_init_mmio(busdev, &g->iomem);
    return 0;
}

static int exynos4_gpio3_realize(SysBusDevice *busdev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(busdev);

    qdev_init_gpio_in(DEVICE(busdev), exynos4_gpio_set_cb,
            GPIO3_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    qdev_init_gpio_out(DEVICE(busdev), g->out_cb,
            GPIO3_NUM_OF_PORTS * GPIO_MAX_PINS_NR);
    memory_region_init_io(&g->iomem, &exynos4_gpio3_mmio_ops, g,
            "exynos4210.gpio3", GPIO3_REGS_MEM_SIZE);
    sysbus_init_mmio(busdev, &g->iomem);
    return 0;
}

static void exynos4_gpio_uninitfn(Object *obj)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(obj);

    if (g->out_cb) {
        g_free(g->out_cb);
        g->out_cb = NULL;
    }
}

static void exynos4_gpio1_reset(DeviceState *dev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(dev);
    Exynos4Gpio12State *g1 = EXYNOS4210_GPIO_1_2(dev);
    unsigned i;

    DPRINT_L2("GPIO1 RESET\n");

    qemu_irq_lower(g1->irq_gpio);
    g1->extint_serv = 0;
    g1->extint_grpfixpri = 0;
    g1->extint_serv_pend = 0;

    for (i = 0; i < GPIO1_PORTINT_NUM; i++) {
        exynos4_gpio_reset_portint(&g->port_ints[i]);
    }

    for (i = 0; i < GPIO1_NUM_OF_PORTS; i++) {
        exynos4_gpio_reset_portgr(&g->ports[i]);
    }
}

static void exynos4_gpio2_reset(DeviceState *dev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(dev);
    Exynos4Gpio12State *g2 = EXYNOS4210_GPIO_1_2(dev);
    unsigned i;

    DPRINT_L2("GPIO2 RESET\n");

    qemu_irq_lower(g2->irq_gpio);
    g2->extint_serv = 0;
    g2->extint_grpfixpri = 0;
    g2->extint_serv_pend = 0;

    for (i = 0; i < GPIO2_PORTINT_NUM; i++) {
        exynos4_gpio_reset_portint(&g->port_ints[i]);
    }

    for (i = 0; i < GPIO2_NUM_OF_PORTS; i++) {
        exynos4_gpio_reset_portgr(&g->ports[i]);
    }
}

static void exynos4_gpio2x_reset(DeviceState *dev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(dev);
    Exynos4Gpio2XState *g2x = EXYNOS4210_GPIO2X(dev);
    unsigned i;

    DPRINT_L2("GPIO2 X group RESET\n");

    for (i = 0; i < GPIO2X_PORT_IRQ_NUM; i++) {
        qemu_irq_lower(g2x->ext_irq[i]);
    }

    for (i = 0; i < GPIO2X_PORTINT_NUM; i++) {
        exynos4_gpio_reset_portint(&g->port_ints[i]);
    }

    for (i = 0; i < GPIO2X_PORT_NUM; i++) {
        exynos4_gpio_reset_portgr(&g->ports[i]);
    }
}

static void exynos4_gpio3_reset(DeviceState *dev)
{
    Exynos4GpioState *g = EXYNOS4210_GPIO(dev);

    DPRINT_L2("GPIO3 RESET\n");
    exynos4_gpio_reset_portgr(&g->ports[0]);
}

static void exynos4_gpio1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4_gpio1_reset;
    dc->vmsd = &exynos4_gpio1_vmstate;
    SYS_BUS_DEVICE_CLASS(klass)->init = exynos4_gpio1_realize;
}

static void exynos4_gpio2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4_gpio2_reset;
    dc->vmsd = &exynos4_gpio2_vmstate;
    SYS_BUS_DEVICE_CLASS(klass)->init = exynos4_gpio2_realize;
}

static void exynos4_gpio2x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4_gpio2x_reset;
    dc->vmsd = &exynos4_gpio2x_vmstate;
    SYS_BUS_DEVICE_CLASS(klass)->init = exynos4_gpio2x_realize;
}

static void exynos4_gpio3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4_gpio3_reset;
    dc->vmsd = &exynos4_gpio3_vmstate;
    SYS_BUS_DEVICE_CLASS(klass)->init = exynos4_gpio3_realize;
}

static const TypeInfo exynos4_gpio_type_info = {
    .name          = TYPE_EXYNOS4210_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4GpioState),
    .instance_finalize = exynos4_gpio_uninitfn,
    .abstract      = true
};

static const TypeInfo exynos4_gpio_1_2_type_info = {
    .name          = TYPE_EXYNOS4210_GPIO_1_2,
    .parent        = TYPE_EXYNOS4210_GPIO,
    .instance_size = sizeof(Exynos4Gpio12State),
    .abstract      = true
};

static const TypeInfo exynos4_gpio1_type_info = {
    .name          = "exynos4210.gpio1",
    .parent        = TYPE_EXYNOS4210_GPIO_1_2,
    .instance_init = exynos4_gpio1_initfn,
    .class_init    = exynos4_gpio1_class_init,
};

static const TypeInfo exynos4_gpio2_type_info = {
    .name          = "exynos4210.gpio2",
    .parent        = TYPE_EXYNOS4210_GPIO_1_2,
    .instance_init = exynos4_gpio2_initfn,
    .class_init    = exynos4_gpio2_class_init,
};

static const TypeInfo exynos4_gpio2x_type_info = {
    .name          = TYPE_EXYNOS4210_GPIO_2X,
    .parent        = TYPE_EXYNOS4210_GPIO,
    .instance_size = sizeof(Exynos4Gpio2XState),
    .instance_init = exynos4_gpio2x_initfn,
    .class_init    = exynos4_gpio2x_class_init,
};

static const TypeInfo exynos4_gpio3_type_info = {
    .name          = "exynos4210.gpio3",
    .parent        = TYPE_EXYNOS4210_GPIO,
    .instance_init = exynos4_gpio3_initfn,
    .class_init    = exynos4_gpio3_class_init,
};

static void exynos4_gpio_register_types(void)
{
    type_register_static(&exynos4_gpio_type_info);
    type_register_static(&exynos4_gpio_1_2_type_info);
    type_register_static(&exynos4_gpio1_type_info);
    type_register_static(&exynos4_gpio2_type_info);
    type_register_static(&exynos4_gpio2x_type_info);
    type_register_static(&exynos4_gpio3_type_info);
}

type_init(exynos4_gpio_register_types)
