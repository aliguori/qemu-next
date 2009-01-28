/*
 * TI OMAP3 processors emulation.
 *
 * Copyright (C) 2008 yajin <yajin@vm-kernel.org>
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
#include "arm-misc.h"
#include "omap.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "flash.h"
#include "soc_dma.h"
#include "audio/audio.h"

//#define _OMAP3_DEBUG_

#ifdef _OMAP3_DEBUG_
#define OMAP3_DEBUG(x)    do {  printf x ; } while(0)
#else
#define OMAP3_DEBUG(x) 
#endif

static uint32_t omap3_l4ta_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    switch (addr)
    {
        //case 0x00:        /* COMPONENT */
        //    return s->component;
    case 0x20:                 /* AGENT_CONTROL */
        return s->control;

    case 0x24:                 /* AGENT_CONTROL_H */
        return s->control_h;

    case 0x28:                 /* AGENT_STATUS */
        return s->status;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap3_l4ta_write(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    switch (addr)
    {
        //case 0x00:        /* COMPONENT */
        //  OMAP_RO_REG(addr);
        // break;
    case 0x20:                 /* AGENT_CONTROL */
        s->control = value & 0x00000700;
        break;
    case 0x24:                 /* AGENT_CONTROL_H */
        s->control_h = value & 0x100;
        break;
    case 0x28:                 /* AGENT_STATUS */
        if (value & 0x100)
            s->status &= ~0x100;        /* REQ_TIMEOUT */
        break;
    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap3_l4ta_readfn[] = {
    omap_badwidth_read16,
    omap3_l4ta_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap3_l4ta_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_l4ta_write,
};



static struct omap_l4_region_s omap3_l4_region[] = {
    [1] = {0x40800, 0x800, 32}, /* Initiator agent */
    [2] = {0x41000, 0x1000, 32},        /* Link agent */
    [0] = {0x40000, 0x800, 32}, /* Address and protection */

    [3] = {0x002000, 0x1000, 32 | 16 | 8},      /* System Control module */
    [4] = {0x003000, 0x1000, 32 | 16 | 8},      /* L4TA1 */

    [5] = {0x004000, 0x2000, 32},       /*CM Region A */
    [6] = {0x006000, 0x0800, 32},       /*CM Region B */
    [7] = {0x007000, 0x1000, 32 | 16 | 8},      /*  L4TA2 */

    [8] = {0x050000, 0x0400, 32},       /*Display subsystem top */
    [9] = {0x050400, 0x0400, 32},       /*Display controller */
    [10] = {0x050800, 0x0400, 32},      /*RFBI*/
    [11] = {0x050c00, 0x0400, 32},       /*Video encoder */
    [12] = {0x051000, 0x1000, 32 | 16 | 8},     /*  L4TA3 */

    [13] = {0x056000, 0x1000, 32},      /*  SDMA */
    [14] = {0x057000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [15] = {0x060000, 0x1000, 16 | 8},  /*  I2C3 */
    [16] = {0x061000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [17] = {0x062000, 0x1000, 32},      /*  USBTLL */
    [18] = {0x063000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [19] = {0x064000, 0x1000, 32},      /* HS USB HOST */
    [20] = {0x065000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [21] = {0x06a000, 0x1000, 32 | 16 | 8},     /* UART1 */
    [22] = {0x06b000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [23] = {0x06c000, 0x1000, 32 | 16 | 8},     /* UART2 */
    [24] = {0x06d000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [25] = {0x070000, 0x1000, 16 | 8},  /*  I2C1 */
    [26] = {0x071000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [27] = {0x072000, 0x1000, 16 | 8},  /*  I2C2 */
    [28] = {0x073000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [29] = {0x074000, 0x1000, 32},      /*  mcbsp1 */
    [30] = {0x075000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [31] = {0x086000, 0x1000, 32 | 16}, /*  GPTIMER10 */
    [32] = {0x087000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [33] = {0x088000, 0x1000, 32 | 16}, /*  GPTIMER11 */
    [34] = {0x089000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [35] = {0x094000, 0x1000, 32 | 16 | 8},     /*  MAILBOX */
    [36] = {0x095000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [37] = {0x096000, 0x1000, 32},      /*  mcbsp5 */
    [38] = {0x097000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [39] = {0x098000, 0x1000, 32 | 16 | 8},     /*  MCSPI1 */
    [40] = {0x099000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [41] = {0x09a000, 0x1000, 32 | 16 | 8},     /*  MCSPI2 */
    [42] = {0x09b000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [43] = {0x09c000, 0x1000, 32},      /*  MMC/SD/SDIO */
    [44] = {0x09d000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [45] = {0x09e000, 0x1000, 32},      /*  MS-PRO */
    [46] = {0x09f000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [47] = {0x09e000, 0x1000, 32},      /*  MS-PRO */
    [48] = {0x09f000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [49] = {0x0ab000, 0x1000, 32},      /*  HS USB OTG */
    [50] = {0x0ac000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [51] = {0x0ad000, 0x1000, 32},      /*  MMC/SD/SDIO3 */
    [52] = {0x0ae000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [53] = {0x0b0000, 0x1000, 32 | 16}, /*  MG */
    [54] = {0x0b1000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [55] = {0x0b2000, 0x1000, 32},      /*  HDQ/1-WIRE */
    [56] = {0x0b3000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [57] = {0x0b4000, 0x1000, 32},      /*  MMC/SD/SDIO2 */
    [58] = {0x0b5000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [59] = {0x0b6000, 0x1000, 32},      /*  icr mpu  */
    [60] = {0x0b7000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [61] = {0x0b8000, 0x1000, 32 | 16 | 8},     /*  MCSPI3  */
    [62] = {0x0b9000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [63] = {0x0ba000, 0x1000, 32 | 16 | 8},     /*  MCSPI4  */
    [64] = {0x0bb000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [65] = {0x0bc000, 0x4000, 32 | 16 | 8},     /*  CAMERA ISP  */
    [66] = {0x0c0000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [67] = {0x0c7000, 0x1000, 32 | 16}, /*  MODEM  */
    [68] = {0x0c8000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [69] = {0x0c9000, 0x1000, 32 | 16 | 8},     /*  SR1  */
    [70] = {0x0ca000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [71] = {0x0cb000, 0x1000, 32 | 16 | 8},     /*  SR2  */
    [72] = {0x0cc000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [73] = {0x0cd000, 0x1000, 32},      /*  ICR MODEM  */
    [74] = {0x0ce000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [75] = {0x30a000, 0x1000, 32 | 16 | 8},     /*  CONTRL MODULE ID  */
    [76] = {0x30b000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    
    [77] = {0x306000, 0x2000, 32},      /* PRM REGION A  */
    [78] = {0x308000, 0x800, 32},       /* PRM REGION B  */
    [79] = {0x309000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

     /*L4 WAKEUP MEMORY SPACE */
    [80] = {0x304000, 0x1000, 32 | 16}, /* GPTIMER12  */
    [81] = {0x305000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */
    
    [82] = {0x30a000, 0x800, 32 | 16 | 8},        /*TAP.undocument*/
    [83] = {0x30a800, 0x1000, 32 | 16 | 8},     /*  L4TA4 */


    [84] = {0x310000, 0x1000, 32 | 16 | 8},     /* GPIO1  */
    [85] = {0x311000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [86] = {0x314000, 0x1000, 32 | 16}, /* WDTIMER2  */
    [87] = {0x315000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [88] = {0x318000, 0x1000, 32 | 16}, /* GPTIMER1  */
    [89] = {0x319000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    [90] = {0x320000, 0x1000, 32 | 16}, /* 32K Timer */
    [91] = {0x321000, 0x1000, 32 | 16 | 8},     /*  L4TA2 */

    [92] = {0x328000, 0x800, 32 | 16 | 8},      /* AP  */
    [93] = {0x328800, 0x800, 32 | 16 | 8},      /* IP  */
    [94] = {0x329000, 0x1000, 32 | 16 | 8},     /* LA  */
    [95] = {0x32a000, 0x800, 32 | 16 | 8},      /* LA  */
    [96] = {0x340000, 0x1000, 32 | 16 | 8},     /*  L4TA4 */

    /*L4 Peripheral MEMORY SPACE */
    [97] = {0x1000000, 0x800, 32 | 16 | 8},     /* AP  */
    [98] = {0x1000800, 0x800, 32 | 16 | 8},     /* IP  */
    [99] = {0x1001000, 0x1000, 32 | 16 | 8},    /* LA  */

    [100] = {0x1020000, 0x1000, 32 | 16 | 8},    /* UART3 */
    [101] = {0x1021000, 0x1000, 32 | 16 | 8},    /*  L4TA4 */

    [102] = {0x1022000, 0x1000, 32},     /* MCBSP 2 */
    [103] = {0x1023000, 0x1000, 32 | 16 | 8},    /*  L4TA4 */
    
    [104] = {0x1024000, 0x1000, 32},    /* MCBSP 3 */
    [105] = {0x1025000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [106] = {0x1026000, 0x1000, 32},    /* MCBSP 4 */
    [107] = {0x1027000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [108] = {0x1028000, 0x1000, 32},    /* MCBSP 2 (sidetone) */
    [109] = {0x1029000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [110] = {0x102a000, 0x1000, 32},    /* MCBSP 3 (sidetone) */
    [111] = {0x102b000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [112] = {0x1030000, 0x1000, 32 | 16},       /* WDTIMER3  */
    [113] = {0x1031000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [114] = {0x1032000, 0x1000, 32 | 16},       /* GPTIMER2 */
    [115] = {0x1033000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [116] = {0x1034000, 0x1000, 32 | 16},       /* GPTIMER3 */
    [117] = {0x1035000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [118] = {0x1036000, 0x1000, 32 | 16},       /* GPTIMER4 */
    [119] = {0x1037000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [120] = {0x1038000, 0x1000, 32 | 16},       /* GPTIMER5 */
    [121] = {0x1039000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [122] = {0x103a000, 0x1000, 32 | 16},       /* GPTIMER6 */
    [123] = {0x103b000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [124] = {0x103c000, 0x1000, 32 | 16},       /* GPTIMER7 */
    [125] = {0x103d000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [126] = {0x103e000, 0x1000, 32 | 16},       /* GPTIMER8 */
    [127] = {0x103f000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [128] = {0x1040000, 0x1000, 32 | 16},       /* GPTIMER9 */
    [129] = {0x1041000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [130] = {0x1050000, 0x1000, 32 | 16 | 8},   /* GPIO2 */
    [131] = {0x1051000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [132] = {0x1052000, 0x1000, 32 | 16 | 8},   /* GPIO3 */
    [133] = {0x1053000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [134] = {0x1054000, 0x1000, 32 | 16 | 8},   /* GPIO4 */
    [135] = {0x1055000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [136] = {0x1056000, 0x1000, 32 | 16 | 8},   /* GPIO5 */
    [137] = {0x1057000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [138] = {0x1058000, 0x1000, 32 | 16 | 8},   /* GPIO6 */
    [139] = {0x1059000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    /*L4 Emulation MEMORY SPACE */
    [140] = {0xc006000, 0x800, 32 | 16 | 8},    /* AP  */
    [141] = {0xc006800, 0x800, 32 | 16 | 8},    /* IP  */
    [142] = {0xc007000, 0x1000, 32 | 16 | 8},   /* LA  */
    [143] = {0xc008000, 0x800, 32 | 16 | 8},    /* DAP  */

    [144] = {0xc010000, 0x8000, 32 | 16 | 8},   /* MPU Emulation */
    [145] = {0xc018000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [146] = {0xc019000, 0x8000, 32},    /* TPIU */
    [147] = {0xc01a000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [148] = {0xc01b000, 0x8000, 32},    /* ETB */
    [149] = {0xc01c000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [150] = {0xc01d000, 0x8000, 32},    /* DAOCTL */
    [151] = {0xc01e000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [152] = {0xc706000, 0x2000, 32},    /* PR Region A */
    [153] = {0xc706800, 0x800, 32},     /* PR Region B */
    [154] = {0xc709000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [155] = {0xc710000, 0x1000, 32 | 16 | 8},   /* GPIO1 */
    [156] = {0xc711000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [157] = {0xc714000, 0x1000, 32 | 16},       /* WDTIMER 2 */
    [158] = {0xc715000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [159] = {0xc718000, 0x1000, 32 | 16 | 8},   /* GPTIMER 1 */
    [160] = {0xc719000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [161] = {0xc720000, 0x1000, 32 | 16},       /* 32k timer */
    [162] = {0xc721000, 0x1000, 32 | 16 | 8},   /*  L4TA4 */

    [163] = {0xc728000, 0x800, 32 | 16 | 8},    /* AP  */
    [164] = {0xc728800, 0x800, 32 | 16 | 8},    /* IP  */
    [165] = {0xc729000, 0x1000, 32 | 16 | 8},   /* LA  */
    [166] = {0xc72a000, 0x800, 32 | 16 | 8},    /* DAP  */

};
static struct omap_l4_agent_info_s omap3_l4_agent_info[] = {
    {0, 0, 2, 1},                /* System Control module */
    {1, 5, 3, 2},                /* CM */
    {2, 77, 3, 2},               /* PRM */
    {3, 86, 2, 1},               /* WDTIMER 2 */
    {4, 3, 2, 1},                 /* SCM */
    {5, 88, 2, 1},               /* GP TIMER 1 */
    {6, 114, 2, 1},              /* GP TIMER 2 */
    {7, 116, 2, 1},              /* GP TIMER 3 */
    {8, 118, 2, 1},              /* GP TIMER 4 */
    {9, 120, 2, 1},              /* GP TIMER 5 */
    {10, 122, 2, 1},              /* GP TIMER 6 */
    {11, 124, 2, 1},              /* GP TIMER 7 */
    {12, 126, 2, 1},              /* GP TIMER 8 */
    {13, 128, 2, 1},              /* GP TIMER 9 */
    {14, 31, 2, 1},              /* GP TIMER 10 */
    {15, 33, 2, 1},              /* GP TIMER 11 */
    {16, 80, 2, 1},              /* GP TIMER 12 */
    {17, 90, 2, 1},                /* 32K Sync timer */
    {18, 21, 2, 1},                /* uart1 */
    {19, 23, 2, 1},                /* uart2 */
    {20, 100, 2, 1},              /* uart3 */
    {21, 8, 5, 4},                /* Display */
    {22, 84, 2, 1},               /* GPIO 1 */
    {23, 130, 2, 1},              /* GPIO 2 */
    {24, 132, 2, 1},              /* GPIO 3 */
    {25, 134, 2, 1},              /* GPIO 4 */
    {26, 136, 2, 1},              /* GPIO 5 */
    {27, 138, 2, 1},              /* GPIO 6 */
    {28,82, 2, 1},                 /* TAP */
    {29,43, 2, 1},                 /* MMC1 */
    {30,57, 2, 1},                 /* MMC2 */
    {31,51, 2, 1},                 /* MMC3 */
    {32,25, 2, 1},                 /* I2C1 */
    {33,27, 2, 1},                 /* I2C2 */
    {34,15, 2, 1},                 /* I2C3 */
    
    
};

static struct omap_target_agent_s *omap3_l4ta_get(struct omap_l4_s *bus, int cs)
{
    int i, iomemtype;
    struct omap_target_agent_s *ta = 0;
    struct omap_l4_agent_info_s *info = 0;

    for (i = 0; i < bus->ta_num; i++)
        if (omap3_l4_agent_info[i].ta == cs)
        {
            ta = &bus->ta[i];
            info = &omap3_l4_agent_info[i];
            break;
        }
    if (!ta)
    {
        fprintf(stderr, "%s: bad target agent (%i)\n", __FUNCTION__, cs);
        exit(-1);
    }

    ta->bus = bus;
    ta->start = &omap3_l4_region[info->region];
    ta->regions = info->regions;

    ta->component = ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    ta->status = 0x00000000;
    ta->control = 0x00000200;   /* XXX 01000200 for L4TAO */

    iomemtype = l4_register_io_memory(0, omap3_l4ta_readfn,
                                      omap3_l4ta_writefn, ta);
    ta->base = omap_l4_attach(ta, info->ta_region, iomemtype);

    return ta;
}


struct omap3_prm_s
{
    qemu_irq irq[3];
    struct omap_mpu_state_s *mpu;

    /*IVA2_PRM Register */
    uint32_t rm_rstctrl_iva2;   /*0x4830 6050 */
    uint32_t rm_rstst_iva2;     /*0x4830 6058 */
    uint32_t pm_wkdep_iva2;     /*0x4830 60C8 */
    uint32_t pm_pwstctrl_iva2;  /*0x4830 60E0 */
    uint32_t pm_pwstst_iva2;    /*0x4830 60E4 */
    uint32_t pm_prepwstst_iva2; /*0x4830 60E8 */
    uint32_t pm_irqstatus_iva2; /*0x4830 60F8 */
    uint32_t pm_irqenable_iva2; /*0x4830 60FC */

    /*OCP_System_Reg_PRM Registerr */
    uint32_t prm_revision;      /*0x4830 6804 */
    uint32_t prm_sysconfig;     /*0x4830 6814 */
    uint32_t prm_irqstatus_mpu; /*0x4830 6818 */
    uint32_t prm_irqenable_mpu; /*0x4830 681c */

    /*MPU_PRM Register */
    uint32_t rm_rstst_mpu;      /*0x4830 6958 */
    uint32_t pm_wkdep_mpu;      /*0x4830 69c8 */
    uint32_t pm_evgenctrl_mpu;  /*0x4830 69d4 */
    uint32_t pm_evgenontim_mpu; /*0x4830 69d8 */
    uint32_t pm_evgenofftim_mpu;        /*0x4830 69dc */
    uint32_t pm_pwstctrl_mpu;   /*0x4830 69e0 */
    uint32_t pm_pwstst_mpu;     /*0x4830 69e4 */
    uint32_t pm_perpwstst_mpu;  /*0x4830 69e8 */

    /*CORE_PRM Register */
    uint32_t rm_rstst_core;     /*0x4830 6a58 */
    uint32_t pm_wken1_core;     /*0x4830 6aa0 */
    uint32_t pm_mpugrpsel1_core;        /*0x4830 6aa4 */
    uint32_t pm_iva2grpsel1_core;       /*0x4830 6aa8 */
    uint32_t pm_wkst1_core;     /*0x4830 6ab0 */
    uint32_t pm_wkst3_core;     /*0x4830 6ab8 */
    uint32_t pm_pwstctrl_core;  /*0x4830 6ae0 */
    uint32_t pm_pwstst_core;    /*0x4830 6ae4 */
    uint32_t pm_prepwstst_core; /*0x4830 6ae8 */
    uint32_t pm_wken3_core;     /*0x4830 6af0 */
    uint32_t pm_iva2grpsel3_core;       /*0x4830 6af4 */
    uint32_t pm_mpugrpsel3_core;        /*0x4830 6af8 */

    /*SGX_PRM Register */
    uint32_t rm_rstst_sgx;      /*0x4830 6b58 */
    uint32_t pm_wkdep_sgx;      /*0x4830 6bc8 */
    uint32_t pm_pwstctrl_sgx;   /*0x4830 6be0 */
    uint32_t pm_pwstst_sgx;     /*0x4830 6be4 */
    uint32_t pm_prepwstst_sgx;  /*0x4830 6be8 */

    /*WKUP_PRM Register */
    uint32_t pm_wken_wkup;      /*0x4830 6ca0 */
    uint32_t pm_mpugrpsel_wkup; /*0x4830 6ca4 */
    uint32_t pm_iva2grpsel_wkup;        /*0x4830 6ca8 */
    uint32_t pm_wkst_wkup;      /*0x4830 6cb0 */

    /*Clock_Control_Reg_PRM Register */
    uint32_t prm_clksel;        /*0x4830 6D40 */
    uint32_t prm_clkout_ctrl;   /*0x4830 6D70 */

    /*DSS_PRM Register */
    uint32_t rm_rstst_dss;      /*0x4830 6e58 */
    uint32_t pm_wken_dss;       /*0x4830 6ea0 */
    uint32_t pm_wkdep_dss;      /*0x4830 6ec8 */
    uint32_t pm_pwstctrl_dss;   /*0x4830 6ee0 */
    uint32_t pm_pwstst_dss;     /*0x4830 6ee4 */
    uint32_t pm_prepwstst_dss;  /*0x4830 6ee8 */

    /*CAM_PRM Register */
    uint32_t rm_rstst_cam;      /*0x4830 6f58 */
    uint32_t pm_wken_cam;       /*0x4830 6fc8 */
    uint32_t pm_pwstctrl_cam;   /*0x4830 6fe0 */
    uint32_t pm_pwstst_cam;     /*0x4830 6fe4 */
    uint32_t pm_prepwstst_cam;  /*0x4830 6fe8 */

    /*PER_PRM Register */
    uint32_t rm_rstst_per;      /*0x4830 7058 */
    uint32_t pm_wken_per;       /*0x4830 70a0 */
    uint32_t pm_mpugrpsel_per;  /*0x4830 70a4 */
    uint32_t pm_iva2grpsel_per; /*0x4830 70a8 */
    uint32_t pm_wkst_per;       /*0x4830 70b0 */
    uint32_t pm_wkdep_per;      /*0x4830 70c8 */
    uint32_t pm_pwstctrl_per;   /*0x4830 70e0 */
    uint32_t pm_pwstst_per;     /*0x4830 70e4 */
    uint32_t pm_perpwstst_per;  /*0x4830 70e8 */

    /*EMU_PRM Register */
    uint32_t rm_rstst_emu;      /*0x4830 7158 */
    uint32_t pm_pwstst_emu;     /*0x4830 71e4 */

    /*Global_Reg_PRM Register */
    uint32_t prm_vc_smps_sa;    /*0x4830 7220 */
    uint32_t prm_vc_smps_vol_ra;        /*0x4830 7224 */
    uint32_t prm_vc_smps_cmd_ra;        /*0x4830 7228 */
    uint32_t prm_vc_cmd_val_0;  /*0x4830 722c */
    uint32_t prm_vc_cmd_val_1;  /*0x4830 7230 */
    uint32_t prm_vc_hc_conf;    /*0x4830 7234 */
    uint32_t prm_vc_i2c_cfg;    /*0x4830 7238 */
    uint32_t prm_vc_bypass_val; /*0x4830 723c */
    uint32_t prm_rstctrl;       /*0x4830 7250 */
    uint32_t prm_rsttimer;      /*0x4830 7254 */
    uint32_t prm_rstst;         /*0x4830 7258 */
    uint32_t prm_voltctrl;      /*0x4830 7260 */
    uint32_t prm_sram_pcharge;  /*0x4830 7264 */
    uint32_t prm_clksrc_ctrl;   /*0x4830 7270 */
    uint32_t prm_obs;           /*0x4830 7280 */
    uint32_t prm_voltsetup1;    /*0x4830 7290 */
    uint32_t prm_voltoffset;    /*0x4830 7294 */
    uint32_t prm_clksetup;      /*0x4830 7298 */
    uint32_t prm_polctrl;       /*0x4830 729c */
    uint32_t prm_voltsetup2;    /*0x4830 72a0 */

    /*NEON_PRM Register */
    uint32_t rm_rstst_neon;     /*0x4830 7358 */
    uint32_t pm_wkdep_neon;     /*0x4830 73c8 */
    uint32_t pm_pwstctrl_neon;  /*0x4830 73e0 */
    uint32_t pm_pwstst_neon;    /*0x4830 73e4 */
    uint32_t pm_prepwstst_neon; /*0x4830 73e8 */

    /*USBHOST_PRM Register */
    uint32_t rm_rstst_usbhost;  /*0x4830 7458 */
    uint32_t rm_wken_usbhost;   /*0x4830 74a0 */
    uint32_t rm_mpugrpsel_usbhost;      /*0x4830 74a4 */
    uint32_t rm_iva2grpsel_usbhost;     /*0x4830 74a8 */
    uint32_t rm_wkst_usbhost;   /*0x4830 74b0 */
    uint32_t rm_wkdep_usbhost;  /*0x4830 74c8 */
    uint32_t rm_pwstctrl_usbhost;       /*0x4830 74e0 */
    uint32_t rm_pwstst_usbhost; /*0x4830 74e4 */
    uint32_t rm_prepwstst_usbhost;      /*0x4830 74e8 */

};

static void omap3_prm_reset(struct omap3_prm_s *s)
{
    s->rm_rstctrl_iva2 = 0x7;
    s->rm_rstst_iva2 = 0x1;
    s->pm_wkdep_iva2 = 0xb3;
    s->pm_pwstctrl_iva2 = 0xff0f07;
    s->pm_pwstst_iva2 = 0xff7;
    s->pm_prepwstst_iva2 = 0x0;
    s->pm_irqstatus_iva2 = 0x0;
    s->pm_irqenable_iva2 = 0x0;

    s->prm_revision = 0x10;
    s->prm_sysconfig = 0x1;
    s->prm_irqstatus_mpu = 0x0;
    s->prm_irqenable_mpu = 0x0;

    s->rm_rstst_mpu = 0x1;
    s->pm_wkdep_mpu = 0xa5;
    s->pm_evgenctrl_mpu = 0x12;
    s->pm_evgenontim_mpu = 0x0;
    s->pm_evgenofftim_mpu = 0x0;
    s->pm_pwstctrl_mpu = 0x30107;
    s->pm_pwstst_mpu = 0xc7;
    s->pm_pwstst_mpu = 0x0;

    s->rm_rstst_core = 0x1;
    s->pm_wken1_core = 0xc33ffe18;
    s->pm_mpugrpsel1_core = 0xc33ffe18;
    s->pm_iva2grpsel1_core = 0xc33ffe18;
    s->pm_wkst1_core = 0x0;
    s->pm_wkst3_core = 0x0;
    s->pm_pwstctrl_core = 0xf0307;
    s->pm_pwstst_core = 0xf7;
    s->pm_prepwstst_core = 0x0;
    s->pm_wken3_core = 0x4;
    s->pm_iva2grpsel3_core = 0x4;
    s->pm_mpugrpsel3_core = 0x4;

    s->rm_rstst_sgx = 0x1;
    s->pm_wkdep_sgx = 0x16;
    s->pm_pwstctrl_sgx = 0x30107;
    s->pm_pwstst_sgx = 0x3;
    s->pm_prepwstst_sgx = 0x0;

    s->pm_wken_wkup = 0x3cb;
    s->pm_mpugrpsel_wkup = 0x3cb;
    s->pm_iva2grpsel_wkup = 0x0;
    s->pm_wkst_wkup = 0x0;

    s->prm_clksel = 0x4;
    s->prm_clkout_ctrl = 0x80;

    s->rm_rstst_dss = 0x1;
    s->pm_wken_dss = 0x1;
    s->pm_wkdep_dss = 0x16;
    s->pm_pwstctrl_dss = 0x30107;
    s->pm_pwstst_dss = 0x3;
    s->pm_prepwstst_dss = 0x0;

    s->rm_rstst_cam = 0x1;
    s->pm_wken_cam = 0x16;
    s->pm_pwstctrl_cam = 0x30107;
    s->pm_pwstst_cam = 0x3;
    s->pm_prepwstst_cam = 0x0;

    s->rm_rstst_per = 0x1;
    s->pm_wken_per = 0x3efff;
    s->pm_mpugrpsel_per = 0x3efff;
    s->pm_iva2grpsel_per = 0x3efff;
    s->pm_wkst_per = 0x0;
    s->pm_wkdep_per = 0x17;
    s->pm_pwstctrl_per = 0x30107;
    s->pm_pwstst_per = 0x7;
    s->pm_perpwstst_per = 0x0;

    s->rm_rstst_emu = 0x1;
    s->pm_pwstst_emu = 0x13;

    s->prm_vc_smps_sa = 0x0;
    s->prm_vc_smps_vol_ra = 0x0;
    s->prm_vc_smps_cmd_ra = 0x0;
    s->prm_vc_cmd_val_0 = 0x0;
    s->prm_vc_cmd_val_1 = 0x0;
    s->prm_vc_hc_conf = 0x0;
    s->prm_vc_i2c_cfg = 0x18;
    s->prm_vc_bypass_val = 0x0;
    s->prm_rstctrl = 0x0;
    s->prm_rsttimer = 0x1006;
    s->prm_rstst = 0x1;
    s->prm_voltctrl = 0x0;
    s->prm_sram_pcharge = 0x50;
    s->prm_clksrc_ctrl = 0x43;
    s->prm_obs = 0x0;
    s->prm_voltsetup1 = 0x0;
    s->prm_voltoffset = 0x0;
    s->prm_clksetup = 0x0;
    s->prm_polctrl = 0xa;
    s->prm_voltsetup2 = 0x0;

    s->rm_rstst_neon = 0x1;
    s->pm_wkdep_neon = 0x2;
    s->pm_pwstctrl_neon = 0x7;
    s->pm_pwstst_neon = 0x3;
    s->pm_prepwstst_neon = 0x0;

    s->rm_rstst_usbhost = 0x1;
    s->rm_wken_usbhost = 0x1;
    s->rm_mpugrpsel_usbhost = 0x1;
    s->rm_iva2grpsel_usbhost = 0x1;
    s->rm_wkst_usbhost = 0x0;
    s->rm_wkdep_usbhost = 0x17;
    s->rm_pwstctrl_usbhost = 0x30107;
    s->rm_pwstst_usbhost = 0x3;
    s->rm_prepwstst_usbhost = 0x0;

}

static uint32_t omap3_prm_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_prm_s *s = (struct omap3_prm_s *) opaque;

    switch (addr)
    {
    case 0x50:
    	return s->rm_rstctrl_iva2;
    case 0x58:
    	return s->rm_rstst_iva2;
    case 0xc8:
    	return s->pm_wkdep_iva2 ;
    case 0xe0:
    	return s->pm_pwstctrl_iva2;
    case 0xe4:
    	return s->pm_pwstst_iva2;
    case 0xe8:
    	return s->pm_prepwstst_iva2;
    case 0xf8:
    	return s->pm_irqstatus_iva2;
    case 0xfc:
    	return s->pm_irqenable_iva2;

    case 0x804:
    	return s->prm_revision;
    case 0x814:
    	return s->prm_sysconfig;
    case 0x818:
    	return s->prm_irqstatus_mpu;
    case 0x81c:
    	return s->prm_irqenable_mpu;

    case 0x958:
    	return s->rm_rstst_mpu;
    case 0x9c8:
    	return s->pm_wkdep_mpu;
    case 0x9d4:
    	return s->pm_evgenctrl_mpu;
    case 0x9d8:
    	return s->pm_evgenontim_mpu;
    case 0x9dc:
    	return s->pm_evgenofftim_mpu;
    case 0x9e0:
    	return s->pm_pwstctrl_mpu;
    case 0x9e4:
    	return s->pm_pwstst_mpu;
    case 0x9e8:
    	return s->pm_perpwstst_mpu;

    case 0xa58:
    	return s->rm_rstst_core;
    case 0xaa0:
    	return s->pm_wken1_core;
    case 0xaa4:
    	return s->pm_mpugrpsel1_core;
    case 0xaa8:
    	return s->pm_iva2grpsel1_core;
    case 0xab0:
    	return s->pm_wkst1_core;
    case 0xab8:
    	return s->pm_wkst3_core;
    case 0xae0:
    	return s->pm_pwstctrl_core;
    case 0xae4:
    	return s->pm_pwstst_core;
    case 0xae8:
    	return s->pm_prepwstst_core;

    case 0xb58:
    	return s->rm_rstst_sgx;
    case 0xbc8:
    	return s->pm_wkdep_sgx;
    case 0xbe0:
    	return s->pm_pwstctrl_sgx;
    case 0xbe4:
    	return s->pm_pwstst_sgx;
    case 0xbe8:
    	return s->pm_prepwstst_sgx;

    	
    case 0xca0:
    	return s->pm_wken_wkup;
   	 case 0xca4:
    	return s->pm_mpugrpsel_wkup ;
    case 0xca8:
    	return s->pm_iva2grpsel_wkup ;
	 case 0xcb0:
    	return s->pm_wkst_wkup ;

    	
    case 0xd40:
        return s->prm_clksel;
    case 0xd70:
    	return s->prm_clkout_ctrl;

     case 0xe58:
     	return s->rm_rstst_dss;
     case 0xea0:
     	return s->pm_wken_dss;
     case 0xec8:
     	return s->pm_wkdep_dss;
     case 0xee0:
     	return s->pm_pwstctrl_dss;
     case 0xee4:
     	return s->pm_pwstst_dss;
     case 0xee8:
     	return s->pm_prepwstst_dss;

     case 0xf58:
     	return s->rm_rstst_cam;
     case 0xfc8:
    	return s->pm_wken_cam ;
    case 0xfe0:
    	return s->pm_pwstctrl_cam;
    case 0xfe4:
    	return s->pm_pwstst_cam;
    case 0xfe8:
    	return s->pm_prepwstst_cam;

    case 0x1058:
    	return s->rm_rstst_per;
    case 0x10a0:
    	return s->pm_wken_per ;
    case 0x10a4:
    	return s->pm_mpugrpsel_per;
    case 0x10a8:
    	return s->pm_iva2grpsel_per;
    case 0x10b0:
    	return s->pm_wkst_per;
    case 0x10c8:
    	return s->pm_wkdep_per;
    case 0x10e0:
    	return s->pm_pwstctrl_per;
    case 0x10e4:
    	return s->pm_pwstst_per;
    case 0x10e8:
    	return s->pm_perpwstst_per;

    	
    case 0x1220:
    	return s->prm_vc_smps_sa;
    case 0x1224:
    	return s->prm_vc_smps_vol_ra ;
    case 0x1228:
    	return s->prm_vc_smps_cmd_ra ;
    case 0x122c:
    	return s->prm_vc_cmd_val_0 ;
    case 0x1230:
    	return s->prm_vc_cmd_val_1 ;
    case 0x1234:
    	return s->prm_vc_hc_conf;
    case 0x1238:
    	return s->prm_vc_i2c_cfg;
    case 0x123c:
    	return s->prm_vc_bypass_val;
	case 0x1250:
    	return s->prm_rstctrl;
	case 0x1254:
    	return s->prm_rsttimer;
    case 0x1258:
    	return s->prm_rstst;
    case 0x1260:
    	return s->prm_voltctrl;
    case 0x1264:
    	return s->prm_sram_pcharge;    	
    case 0x1270:
        return s->prm_clksrc_ctrl;
    case 0x1280:
    	return s->prm_obs;
    case 0x1290:
    	return s->prm_voltsetup1;
    case 0x1294:
    	return s->prm_voltoffset;
    case 0x1298:
    	return s->prm_clksetup;
    case 0x129c:
    	return s->prm_polctrl;
    case 0x12a0:
    	return s->prm_voltsetup2;

    case 0x1358:
    	return s->rm_rstst_neon;
    case 0x13c8:
   		return s->pm_wkdep_neon ;
   	case 0x13e0:
   		return s->pm_pwstctrl_neon;
   	case 0x13e4:
   		return s->pm_pwstst_neon;
   	case 0x13e8:
   		return s->pm_prepwstst_neon;

   	case 0x1458:
   		return s->rm_rstst_usbhost;
   case 0x14a0:
   		return s->rm_wken_usbhost ;
   	case 0x14a4:
   		return s->rm_mpugrpsel_usbhost;
   	case 0x14a8:
   		return s->rm_iva2grpsel_usbhost;
   	case 0x14b0:
   		return s->rm_wkst_usbhost;
    case 0x14c8:
    	return s->rm_wkdep_usbhost;
    case 0x14e0:
    	return s->rm_pwstctrl_usbhost;
    case 0x14e4:
    	return s->rm_pwstst_usbhost;
    case 0x14e8:
    	return s->rm_prepwstst_usbhost;

    default:
    	 printf("prm READ offset %x\n",addr);
        exit(-1);
    }
}

static inline void omap3_prm_clksrc_ctrl_update(struct omap3_prm_s *s,
                                                uint32_t value)
{
    if ((value & 0xd0) == 0x40)
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_sys_clk"), 1, 1);
    else if ((value & 0xd0) == 0x80)
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_sys_clk"), 2, 1);
    //OMAP3_DEBUG(("omap3_sys_clk %d \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_sys_clk"))));
}
static void omap3_prm_write(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    struct omap3_prm_s *s = (struct omap3_prm_s *) opaque;

    switch (addr)
    {
    case 0xc8:
    	s->pm_wkdep_iva2 = value & 0xb3;
    	break;
    case 0xe0:
    	s->pm_pwstctrl_iva2 = value & 0xff0f0f;
    	break;
    case 0xe4:
    	s->pm_pwstst_iva2 = value & 0x100ff7;
    	break;
    case 0xe8:
    	s->pm_prepwstst_iva2 = value & 0xff7;
    	break;
    case 0x814:
    	s->prm_sysconfig = value &0x1;
    	break;
    case 0x818:
    	s->prm_irqstatus_mpu = 0x0;
    	break;
    case 0x81c:
    	s->prm_irqenable_mpu = 0x3ffffff;
    	break;
    case 0x9c8:
    	s->pm_wkdep_mpu = value & 0xa5;;
    	break;
    case 0x9e0:
    	s->pm_pwstctrl_mpu = value & 0x3010f;
    	break;
    case 0x9e4:
    	s->pm_pwstst_mpu = value & 0x1000c7;
    	break;
    case 0x9e8:
    	s->pm_perpwstst_mpu = value & 0xc7;
    	break;
    case 0xae0:
    	s->pm_pwstctrl_core = value & 0xf0307;
    	break;
    case 0xae4:
    	s->pm_pwstst_core = value & 0x1000f3;
    	break;
    case 0xae8:
    	s->pm_prepwstst_core = value & 0xf7;
    	break;
    case 0xbc8:
    	s->pm_wkdep_sgx = value & 0x17;
    	break;
   case 0xbe0:
   		s->pm_pwstctrl_sgx = value & 0x30107;
   		break;
   	case 0xbe4:
   		s->pm_pwstst_sgx = value & 0x100003;;
   		break;
   	case 0xbe8:
   		s->pm_prepwstst_sgx = value & 0x3;
   		break;    	
    case 0xca0:
    	s->pm_wken_wkup = value & 0x3cb;
    	break;
    case 0xca4:
    	s->pm_mpugrpsel_wkup = value & 0x3cb;
    	break;
    case 0xca8:
    	s->pm_iva2grpsel_wkup = value & 0x3cb;
    	break;
	 case 0xcac:
    	s->pm_wkst_wkup = value & 0x3cb;
    	break;
    case 0xd40:
        s->prm_clksel = value & 0x7;
        break;
    case 0xec8:
     	 s->pm_wkdep_dss = value & 0x16;
     	 break;
    case 0xee0:
    	s->pm_pwstctrl_dss = value & 0x30107;
    	break;
    case 0xee4:
    	s->pm_pwstst_dss = value & 0x100003;
    	break;
    case 0xee8:
    	s->pm_prepwstst_dss = value &0x3;
    	break;
    case 0xfc8:
    	s->pm_wken_cam = value & 0x16;
     	 break;
    case 0xfe0:
    	s->pm_pwstctrl_cam = value & 0x30107;
    	break;
    case 0xfe4:
    	s->pm_pwstst_cam = value & 0x100003;
    	break;
    case 0xfe8:
    	s->pm_prepwstst_cam = value & 0x3;
    	break;
    case 0x10c8:
    	s->pm_wkdep_per = value & 0x17;
		break;
    case 0x10a0:
    	s->pm_wken_per = value & 0x3efff;
     	break;
    case 0x10e0:
		s->pm_pwstctrl_per = value & 0x30107;
		break;
	case 0x10e4:
		s->pm_pwstst_per = value & 0x100003;;
		break;
	case 0x10e8:
		s->pm_perpwstst_per = value & 0x3;
		break;    	
    case 0x1220:
    	s->prm_vc_smps_sa = value & 0x7f007f;
    	break;
    case 0x1224:
    	s->prm_vc_smps_vol_ra = value & 0xff00ff;
    	break;
    case 0x1228:
    	s->prm_vc_smps_cmd_ra = value & 0xff00ff;
    	break;
    case 0x122c:
    	s->prm_vc_cmd_val_0 = value ;
    	break;
    case 0x1230:
    	s->prm_vc_cmd_val_1 = value ;
    	break;
    case 0x1234:
    	s->prm_vc_hc_conf = value & 0x1f001f;
    	break;
    case 0x1238:
    	s->prm_vc_i2c_cfg = value & 0x3f;
    	break;
    case 0x123c:
    	s->prm_vc_bypass_val = value;
    	break;
    case 0x1250:
    	s->prm_rstctrl = value & 0x7;
    	/*TODO: Software reset*/
    	break;
    case 0x1254:
    	s->prm_rsttimer = value & 0x1fff;
    	break;
    case 0x1258:
    	s->prm_rstst = value & 0x7ff;
    	break;
    case 0x1260:
    	s->prm_voltctrl = value & 0x1f;
    	break;
    case 0x1264:
    	s->prm_sram_pcharge = value &0xff;
    	break;
    case 0x1270:
        s->prm_clksrc_ctrl = value & (0xd8);
        omap3_prm_clksrc_ctrl_update(s, s->prm_clksrc_ctrl);
        break;
    case 0x1290:
    	s->prm_voltsetup1 = value;
    	break;
    case 0x1294:
    	s->prm_voltoffset = value&0xffff;
    	break;
    case 0x1298:
    	s->prm_clksetup = value&0xffff;
    	break;
    case 0x129c:
    	 s->prm_polctrl = value&0xf;
    	 break;
    case 0x12a0:
    	s->prm_voltsetup2 = value & 0xffff;
    	break;
   case 0x13c8:
   		s->pm_wkdep_neon = value & 0x2;
   		break;
   	case 0x13e0:
   		s->pm_pwstctrl_neon = value & 0x7;
   		break;
   	case 0x13e4:
   		s->pm_pwstst_neon = value & 0x100003;
   		break;
   case 0x13e8:
   		s->pm_prepwstst_neon = value & 0x3;
   		break;
   case 0x14a0:
   		s->rm_wken_usbhost = value &0x1;
   		break;
    case 0x14c8:
    	s->rm_wkdep_usbhost = value & 0x17;
    	break;
    case 0x14e0:
    	s->rm_pwstctrl_usbhost = value & 0x30117;
    	break;
    case 0x14e4:
    	s->rm_pwstst_usbhost = value & 0x100002;
    	break;
    case 0x14e8:
    	s->rm_prepwstst_usbhost = value & 0x2;
    	break;

    default:
        printf("omap3_prm_write addr %x value %x \n", addr, value);
        exit(-1);
    }
}


static CPUReadMemoryFunc *omap3_prm_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_prm_read,
};

static CPUWriteMemoryFunc *omap3_prm_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_prm_write,
};

struct omap3_prm_s *omap3_prm_init(struct omap_target_agent_s *ta,
                                   qemu_irq mpu_int, qemu_irq dsp_int,
                                   qemu_irq iva_int,
                                   struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_prm_s *s = (struct omap3_prm_s *) qemu_mallocz(sizeof(*s));

    s->irq[0] = mpu_int;
    s->irq[1] = dsp_int;
    s->irq[2] = iva_int;
    s->mpu = mpu;
    omap3_prm_reset(s);

    iomemtype = l4_register_io_memory(0, omap3_prm_readfn,
                                      omap3_prm_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    omap_l4_attach(ta, 1, iomemtype);

    return s;
}


struct omap3_cm_s
{
    qemu_irq irq[3];
    struct omap_mpu_state_s *mpu;

    /*IVA2_CM Register */
    uint32_t cm_fclken_iva2;    /*0x4800 4000 */
    uint32_t cm_clken_pll_iva2; /*0x4800 4004 */
    uint32_t cm_idlest_iva2;    /*0x4800 4020 */
    uint32_t cm_idlest_pll_iva2;        /*0x4800 4024 */
    uint32_t cm_autoidle_pll_iva2;      /*0x4800 4034 */
    uint32_t cm_clksel1_pll_iva2;       /*0x4800 4040 */
    uint32_t cm_clksel2_pll_iva2;       /*0x4800 4044 */
    uint32_t cm_clkstctrl_iva2; /*0x4800 4048 */
    uint32_t cm_clkstst_iva2;   /*0x4800 404c */

    /*OCP_System_Reg_CM */
    uint32_t cm_revision;       /*0x4800 4800 */
    uint32_t cm_sysconfig;      /*0x4800 4810 */

    /*MPU_CM Register */
    uint32_t cm_clken_pll_mpu;  /*0x4800 4904 */
    uint32_t cm_idlest_mpu;     /*0x4800 4920 */
    uint32_t cm_idlest_pll_mpu; /*0x4800 4924 */
    uint32_t cm_autoidle_pll_mpu;       /*0x4800 4934 */
    uint32_t cm_clksel1_pll_mpu;        /*0x4800 4940 */
    uint32_t cm_clksel2_pll_mpu;        /*0x4800 4944 */
    uint32_t cm_clkstctrl_mpu;  /*0x4800 4948 */
    uint32_t cm_clkstst_mpu;    /*0x4800 494c */

    /*CORE_CM Register */
    uint32_t cm_fclken1_core;   /*0x4800 4a00 */
    uint32_t cm_fclken3_core;   /*0x4800 4a08 */
    uint32_t cm_iclken1_core;   /*0x4800 4a10 */
    uint32_t cm_iclken2_core;   /*0x4800 4a14 */
    uint32_t cm_iclken3_core;   /*0x4800 4a18 */
    uint32_t cm_idlest1_core;   /*0x4800 4a20 */
    uint32_t cm_idlest2_core;   /*0x4800 4a24 */
    uint32_t cm_idlest3_core;   /*0x4800 4a28 */
    uint32_t cm_autoidle1_core; /*0x4800 4a30 */
    uint32_t cm_autoidle2_core; /*0x4800 4a34 */
    uint32_t cm_autoidle3_core; /*0x4800 4a38 */
    uint32_t cm_clksel_core;    /*0x4800 4a40 */
    uint32_t cm_clkstctrl_core; /*0x4800 4a48 */
    uint32_t cm_clkstst_core;   /*0x4800 4a4c */

    /*SGX_CM Register */
    uint32_t cm_fclken_sgx;     /*0x4800 4b00 */
    uint32_t cm_iclken_sgx;     /*0x4800 4b10 */
    uint32_t cm_idlest_sgx;     /*0x4800 4b20 */
    uint32_t cm_clksel_sgx;     /*0x4800 4b40 */
    uint32_t cm_sleepdep_sgx;   /*0x4800 4b44 */
    uint32_t cm_clkstctrl_sgx;  /*0x4800 4b48 */
    uint32_t cm_clkstst_sgx;    /*0x4800 4b4c */

    /*WKUP_CM Register */
    uint32_t cm_fclken_wkup;    /*0x4800 4c00 */
    uint32_t cm_iclken_wkup;    /*0x4800 4c10 */
    uint32_t cm_idlest_wkup;    /*0x4800 4c20 */
    uint32_t cm_autoidle_wkup;  /*0x4800 4c30 */
    uint32_t cm_clksel_wkup;    /*0x4800 4c40 */
    uint32_t cm_c48;                  /*0x4800 4c48 */

    /*Clock_Control_Reg_CM Register */
    uint32_t cm_clken_pll;      /*0x4800 4d00 */
    uint32_t cm_clken2_pll;     /*0x4800 4d04 */
    uint32_t cm_idlest_ckgen;   /*0x4800 4d20 */
    uint32_t cm_idlest2_ckgen;  /*0x4800 4d24 */
    uint32_t cm_autoidle_pll;   /*0x4800 4d30 */
    uint32_t cm_autoidle2_pll;  /*0x4800 4d34 */
    uint32_t cm_clksel1_pll;    /*0x4800 4d40 */
    uint32_t cm_clksel2_pll;    /*0x4800 4d44 */
    uint32_t cm_clksel3_pll;    /*0x4800 4d48 */
    uint32_t cm_clksel4_pll;    /*0x4800 4d4c */
    uint32_t cm_clksel5_pll;    /*0x4800 4d50 */
    uint32_t cm_clkout_ctrl;    /*0x4800 4d70 */

    /*DSS_CM Register */
    uint32_t cm_fclken_dss;     /*0x4800 4e00 */
    uint32_t cm_iclken_dss;     /*0x4800 4e10 */
    uint32_t cm_idlest_dss;     /*0x4800 4e20 */
    uint32_t cm_autoidle_dss;   /*0x4800 4e30 */
    uint32_t cm_clksel_dss;     /*0x4800 4e40 */
    uint32_t cm_sleepdep_dss;   /*0x4800 4e44 */
    uint32_t cm_clkstctrl_dss;  /*0x4800 4e48 */
    uint32_t cm_clkstst_dss;    /*0x4800 4e4c */


    /*CAM_CM Register */
    uint32_t cm_fclken_cam;     /*0x4800 4f00 */
    uint32_t cm_iclken_cam;     /*0x4800 4f10 */
    uint32_t cm_idlest_cam;     /*0x4800 4f20 */
    uint32_t cm_autoidle_cam;   /*0x4800 4f30 */
    uint32_t cm_clksel_cam;     /*0x4800 4f40 */
    uint32_t cm_sleepdep_cam;   /*0x4800 4f44 */
    uint32_t cm_clkstctrl_cam;  /*0x4800 4f48 */
    uint32_t cm_clkstst_cam;    /*0x4800 4f4c */

    /*PER_CM Register */
    uint32_t cm_fclken_per;     /*0x4800 5000 */
    uint32_t cm_iclken_per;     /*0x4800 5010 */
    uint32_t cm_idlest_per;     /*0x4800 5020 */
    uint32_t cm_autoidle_per;   /*0x4800 5030 */
    uint32_t cm_clksel_per;     /*0x4800 5040 */
    uint32_t cm_sleepdep_per;   /*0x4800 5044 */
    uint32_t cm_clkstctrl_per;  /*0x4800 5048 */
    uint32_t cm_clkstst_per;    /*0x4800 504c */

    /*EMU_CM Register */
    uint32_t cm_clksel1_emu;    /*0x4800 5140 */
    uint32_t cm_clkstctrl_emu;  /*0x4800 5148 */
    uint32_t cm_clkstst_emu;    /*0x4800 514c */
    uint32_t cm_clksel2_emu;    /*0x4800 5150 */
    uint32_t cm_clksel3_emu;    /*0x4800 5154 */

    /*Global_Reg_CM Register */
    uint32_t cm_polctrl;        /*0x4800 529c */

    /*NEON_CM Register */
    uint32_t cm_idlest_neon;    /*0x4800 5320 */
    uint32_t cm_clkstctrl_neon; /*0x4800 5348 */

    /*USBHOST_CM Register */
    uint32_t cm_fclken_usbhost; /*0x4800 5400 */
    uint32_t cm_iclken_usbhost; /*0x4800 5410 */
    uint32_t cm_idlest_usbhost; /*0x4800 5420 */
    uint32_t cm_autoidle_usbhost;       /*0x4800 5430 */
    uint32_t cm_sleepdep_usbhost;       /*0x4800 5444 */
    uint32_t cm_clkstctrl_usbhost;      /*0x4800 5448 */
    uint32_t cm_clkstst_usbhost;        /*0x4800 544c */

};

/*
static inline void omap3_cm_fclken_wkup_update(struct omap3_cm_s *s,
                uint32_t value)
{
	
	if (value & 0x28)
     	omap_clk_onoff(omap_findclk(s->mpu,"omap3_wkup_32k_fclk"), 1);
    else
    	omap_clk_onoff(omap_findclk(s->mpu,"omap3_wkup_32k_fclk"), 0);

    if (value &0x1)
    	omap_clk_onoff(omap_findclk(s->mpu,"omap3_gp1_fclk"), 1);
    else
    	omap_clk_onoff(omap_findclk(s->mpu,"omap3_gp1_fclk"), 0);

}
static inline void omap3_cm_iclken_wkup_update(struct omap3_cm_s *s,
                uint32_t value)
{
	
	if (value & 0x3f)
     	omap_clk_onoff(omap_findclk(s->mpu,"omap3_wkup_l4_iclk"), 1);
    else
    	omap_clk_onoff(omap_findclk(s->mpu,"omap3_wkup_l4_iclk"), 0);

}
*/
static inline void omap3_cm_clksel_wkup_update(struct omap3_cm_s *s,
                                               uint32_t value)
{
    omap_clk gp1_fclk = omap_findclk(s->mpu, "omap3_gp1_fclk");

    if (value & 0x1)
        omap_clk_reparent(gp1_fclk, omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(gp1_fclk, omap_findclk(s->mpu, "omap3_32k_fclk"));
    /*Tell GPTIMER to generate new clk rate */
    omap_gp_timer_change_clk(s->mpu->gptimer[0]);

    OMAP3_DEBUG(("omap3_gp1_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp1_fclk"))));

    /*TODO:CM_USIM_CLK CLKSEL_RM */
}

static inline void omap3_cm_mpu_update(struct omap3_cm_s *s)
{
    uint32_t m, n, divide, m2, cm_clken_pll_mpu;
    uint32_t bypass = 1;

    cm_clken_pll_mpu = s->cm_clken_pll_mpu;
    omap_clk mpu_clk = omap_findclk(s->mpu, "omap3_mpu_clk");

    if ((cm_clken_pll_mpu & 0x7) == 0x5)
    {
        bypass = 1;
    }
    else if ((cm_clken_pll_mpu & 0x7) == 0x7)
    {
        m = (s->cm_clksel1_pll_mpu & 0x7ff00) >> 8;
        if ((m == 0) || (m == 1))
            bypass = 1;
        else
            bypass = 0;
    }
    if (bypass == 1)
    {
        /*BYPASS Model */
        divide = (s->cm_clksel1_pll_mpu & 0x380000) >> 19;
        //OMAP3_DEBUG(("divide %d\n",divide));
        omap_clk_reparent(mpu_clk, omap_findclk(s->mpu, "omap3_core_clk"));
        omap_clk_setrate(mpu_clk, divide, 1);

    }
    else
    {
        n = (s->cm_clksel1_pll_mpu & 0x7F);
        m2 = (s->cm_clksel2_pll_mpu & 0x1F);
        //OMAP3_DEBUG(("M  %d N %d M2 %d \n",m,n,m2 ));
        omap_clk_reparent(mpu_clk, omap_findclk(s->mpu, "omap3_sys_clk"));
        omap_clk_setrate(mpu_clk, (n + 1) * m2, m);
        //OMAP3_DEBUG(("mpu %d \n",omap_clk_getrate(mpu_clk)));

    }

}
static inline void omap3_cm_iva2_update(struct omap3_cm_s *s)
{
    uint32_t m, n, divide, m2, cm_clken_pll_iva2;
    uint32_t bypass = 1;

    cm_clken_pll_iva2 = s->cm_clken_pll_iva2;
    omap_clk iva2_clk = omap_findclk(s->mpu, "omap3_iva2_clk");

    if (((cm_clken_pll_iva2 & 0x7) == 0x5)
        || ((cm_clken_pll_iva2 & 0x7) == 0x1))
    {
        bypass = 1;
    }
    else if ((cm_clken_pll_iva2 & 0x7) == 0x7)
    {
        m = (s->cm_clksel1_pll_iva2 & 0x7ff00) >> 8;
        if ((m == 0) || (m == 1))
            bypass = 1;
        else
            bypass = 0;
    }
    if (bypass == 1)
    {
        /*BYPASS Model */
        divide = (s->cm_clksel1_pll_iva2 & 0x380000) >> 19;
        //OMAP3_DEBUG(("divide %d\n",divide));
        omap_clk_reparent(iva2_clk, omap_findclk(s->mpu, "omap3_core_clk"));
        omap_clk_setrate(iva2_clk, divide, 1);

    }
    else
    {
        n = (s->cm_clksel1_pll_iva2 & 0x7F);
        m2 = (s->cm_clksel2_pll_iva2 & 0x1F);
        //OMAP3_DEBUG(("M  %d N %d M2 %d \n",m,n,m2 ));
        omap_clk_reparent(iva2_clk, omap_findclk(s->mpu, "omap3_sys_clk"));
        omap_clk_setrate(iva2_clk, (n + 1) * m2, m);
        //OMAP3_DEBUG(("iva2_clk %d \n",omap_clk_getrate(iva2_clk)));

    }

}

static inline void omap3_cm_dpll3_update(struct omap3_cm_s *s)
{
    uint32_t m, n, m2, m3, cm_clken_pll;
    uint32_t bypass = 1;

    cm_clken_pll = s->cm_clken_pll;

    /*dpll3 bypass mode. parent clock is always omap3_sys_clk */
    if (((cm_clken_pll & 0x7) == 0x5) || ((cm_clken_pll & 0x7) == 0x6))
    {
        bypass = 1;
    }
    else if ((cm_clken_pll & 0x7) == 0x7)
    {
        m = (s->cm_clksel1_pll & 0x7ff0000) >> 16;
        if ((m == 0) || (m == 1))
            bypass = 1;
        else
            bypass = 0;
    }
    if (bypass == 1)
    {
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_core_clk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_core2_clk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_emu_core_alwon_clk"), 1,
                         1);
    }
    else
    {
        n = (s->cm_clksel1_pll & 0x3f00) >> 8;
        m2 = (s->cm_clksel1_pll & 0xf8000000) >> 27;
        m3 = (s->cm_clksel1_emu & 0x1f0000) >> 16;

        if (s->cm_clksel2_emu&0x80000)
        {
        	/*override control of DPLL3*/
        	m = (s->cm_clksel2_emu&0x7ff)>>8;
        	n =  s->cm_clksel2_emu&0x7f;
        	OMAP3_DEBUG(("DPLL3 override, m 0x%x n 0x%x \n",m,n));
        }

        //OMAP3_DEBUG(("dpll3 cm_clksel1_pll %x m  %d n %d m2 %d  m3 %d\n",s->cm_clksel1_pll,m,n,m2,m3 ));
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_core_clk"), (n + 1) * m2,
                         m);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_core2_clk"), (n + 1) * m2,
                         m * 2);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_emu_core_alwon_clk"),
                         (n + 1) * m3, m * 2);
        OMAP3_DEBUG(("coreclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_core_clk"))));
    }


}


static inline void omap3_cm_dpll4_update(struct omap3_cm_s *s)
{
    uint32_t m, n, m2, m3, m4, m5, m6, cm_clken_pll;
    cm_clken_pll = s->cm_clken_pll;
    uint32_t bypass = 1;

    /*dpll3 bypass mode. parent clock is always omap3_sys_clk */
    /*DPLL4 */
    if ((cm_clken_pll & 0x70000) == 0x10000)
    {
        bypass = 1;
    }
    else if ((cm_clken_pll & 0x70000) == 0x70000)
    {
        m = (s->cm_clksel2_pll & 0x7ff00) >> 8;
        if ((m == 0) || (m == 1))
            bypass = 1;
        else
            bypass = 0;
    }
    if (bypass == 1)
    {
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_96m_fclk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_54m_fclk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_dss1_alwon_fclk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_cam_mclk"), 1, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_per_alwon_clk"), 1, 1);
    }
    else
    {
        n = (s->cm_clksel2_pll & 0x7f);
        m2 = s->cm_clksel3_pll & 0x1f;
        m3 = (s->cm_clksel_dss & 0x1f00) >> 8;
        m4 = s->cm_clksel_dss & 0x1f;
        m5 = s->cm_clksel_cam & 0x1f;
        m6 = (s->cm_clksel1_emu & 0x1f000000) >> 24;

        if (s->cm_clksel3_emu&0x80000)
        {
        	/*override control of DPLL4*/
        	m = (s->cm_clksel3_emu&0x7ff)>>8;
        	n =  s->cm_clksel3_emu&0x7f;
        	OMAP3_DEBUG(("DPLL4 override, m 0x%x n 0x%x \n",m,n));
        }


        //OMAP3_DEBUG(("dpll4 m  %d n %d m2 %d  m3 %d m4 %d m5 %d m6 %d \n",m,n,m2,m3,m4,m5,m6 ));
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_96m_fclk"), (n + 1) * m2,
                         m * 2);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_54m_fclk"), (n + 1) * m3,
                         m * 2);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_dss1_alwon_fclk"),
                         (n + 1) * m4, m * 2);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_cam_mclk"), (n + 1) * m5,
                         m * 2);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_per_alwon_clk"),
                         (n + 1) * m6, m * 2);

        OMAP3_DEBUG(("omap3_96m_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_96m_fclk"))));
        OMAP3_DEBUG(("omap3_54m_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_54m_fclk"))));
        OMAP3_DEBUG(("omap3_dss1_alwon_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_dss1_alwon_fclk"))));
        OMAP3_DEBUG(("omap3_cam_mclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_cam_mclk"))));
        OMAP3_DEBUG(("omap3_per_alwon_clk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_per_alwon_clk"))));
        OMAP3_DEBUG(("omap3_48m_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_48m_fclk"))));
        OMAP3_DEBUG(("omap3_12m_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_12m_fclk"))));

        		printf("omap3_cm_dpll4_update \n");

    }

}

static inline void omap3_cm_dpll5_update(struct omap3_cm_s *s)
{
	 uint32_t m, n, m2, cm_idlest2_ckgen;
    uint32_t bypass = 1;

    cm_idlest2_ckgen = s->cm_idlest2_ckgen;;

    /*dpll5 bypass mode */
    if ((cm_idlest2_ckgen & 0x1) == 0x0) 
    {
        bypass = 1;
    }

    if (bypass == 1)
    {
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_120m_fclk"), 1, 1);
    }
    else
    {
    	 m = (s->cm_clksel4_pll & 0x7ff00)>>8;
        n = s->cm_clksel4_pll & 0x3f00;
        m2 = s->cm_clksel5_pll & 0x1f;

        OMAP3_DEBUG(("dpll5 m %d n %d m2 %d\n",m,n,m2 ));
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_120m_fclk"), (n + 1) * m2,
                         m);
        OMAP3_DEBUG(("omap3_120m_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_120m_fclk"))));
    }


}
static inline void omap3_cm_48m_update(struct omap3_cm_s *s)
{
    if (s->cm_clksel1_pll & 0x8)
    {
        /*parent is sysaltclk */
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_48m_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_altclk"));
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_12m_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_altclk"));
        /*TODO:need to set rate ? */

    }
    else
    {
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_12m_fclk"),
                          omap_findclk(s->mpu, "omap3_96m_fclk"));
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_48m_fclk"),
                          omap_findclk(s->mpu, "omap3_96m_fclk"));
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_48m_fclk"), 2, 1);
        omap_clk_setrate(omap_findclk(s->mpu, "omap3_12m_fclk"), 8, 1);

    }

}

static inline void omap3_cm_gp10_update(struct omap3_cm_s *s)
{
    omap_clk gp10_fclk = omap_findclk(s->mpu, "omap3_gp10_fclk");

    if (s->cm_clksel_core & 0x40)
        omap_clk_reparent(gp10_fclk, omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(gp10_fclk, omap_findclk(s->mpu, "omap3_32k_fclk"));

    /*Tell GPTIMER10 to generate new clk rate */
    omap_gp_timer_change_clk(s->mpu->gptimer[9]);
    OMAP3_DEBUG(("omap3_gp10_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp10_fclk"))));
}

static inline void omap3_cm_gp11_update(struct omap3_cm_s *s)
{
    omap_clk gp11_fclk = omap_findclk(s->mpu, "omap3_gp11_fclk");

    if (s->cm_clksel_core & 0x80)
        omap_clk_reparent(gp11_fclk, omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(gp11_fclk, omap_findclk(s->mpu, "omap3_32k_fclk"));
    /*Tell GPTIMER10 to generate new clk rate */
    omap_gp_timer_change_clk(s->mpu->gptimer[10]);
    OMAP3_DEBUG(("omap3_gp11_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp11_fclk"))));
}

static inline void omap3_cm_l3clk_update(struct omap3_cm_s *s)
{
    omap_clk l3_iclk = omap_findclk(s->mpu, "omap3_l3_iclk");
    if ((s->cm_clksel_core & 0x3) == 0x1)
        omap_clk_setrate(l3_iclk, 1, 1);
    else if ((s->cm_clksel_core & 0x3) == 0x2)
        omap_clk_setrate(l3_iclk, 2, 1);
}

static inline void omap3_cm_l4clk_update(struct omap3_cm_s *s)
{
    omap_clk l4_iclk = omap_findclk(s->mpu, "omap3_l4_iclk");
    if ((s->cm_clksel_core & 0xc) == 0x4)
        omap_clk_setrate(l4_iclk, 1, 1);
    else if ((s->cm_clksel_core & 0xc) == 0x8)
        omap_clk_setrate(l4_iclk, 2, 1);
}

static inline void omap3_cm_per_gptimer_update(struct omap3_cm_s *s)
{
    uint32_t cm_clksel_per = s->cm_clksel_per;

    if (cm_clksel_per & 0x1)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp2_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp2_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[1]);

    if (cm_clksel_per & 0x2)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp3_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp3_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[2]);

    if (cm_clksel_per & 0x4)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp4_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp4_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[3]);

    if (cm_clksel_per & 0x8)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp5_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp5_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[4]);

    if (cm_clksel_per & 0x10)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp6_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp6_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[5]);
    
    if (cm_clksel_per & 0x20)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp7_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp7_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[6]);


    if (cm_clksel_per & 0x40)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp8_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp8_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[7]);
    
    if (cm_clksel_per & 0x80)
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp9_fclk"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
    else
        omap_clk_reparent(omap_findclk(s->mpu, "omap3_gp9_fclk"),
                          omap_findclk(s->mpu, "omap3_32k_fclk"));
    omap_gp_timer_change_clk(s->mpu->gptimer[8]);

    /*TODO:Tell GPTIMER to generate new clk rate */
    OMAP3_DEBUG(("omap3_gp2_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp2_fclk"))));
    OMAP3_DEBUG(("omap3_gp3_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp3_fclk"))));
	OMAP3_DEBUG(("omap3_gp4_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp4_fclk"))));
    OMAP3_DEBUG(("omap3_gp5_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp5_fclk"))));
    OMAP3_DEBUG(("omap3_gp6_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp6_fclk"))));
    OMAP3_DEBUG(("omap3_gp7_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp7_fclk"))));
    OMAP3_DEBUG(("omap3_gp8_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp8_fclk"))));
    OMAP3_DEBUG(("omap3_gp9_fclk %lld \n",omap_clk_getrate(omap_findclk(s->mpu, "omap3_gp9_fclk"))));
}

static inline void omap3_cm_clkout2_update(struct omap3_cm_s *s)
{
	uint32 divor;
	
	if (!s->cm_clkout_ctrl&0x80)
		return;

	switch (s->cm_clkout_ctrl&0x3)
	{
		case 0x0:
			omap_clk_reparent(omap_findclk(s->mpu, "omap3_sys_clkout2"),
                          omap_findclk(s->mpu, "omap3_core_clk"));
			break;
		case 0x1:
			omap_clk_reparent(omap_findclk(s->mpu, "omap3_sys_clkout2"),
                          omap_findclk(s->mpu, "omap3_sys_clk"));
			break;
		case 0x2:
			omap_clk_reparent(omap_findclk(s->mpu, "omap3_sys_clkout2"),
                          omap_findclk(s->mpu, "omap3_96m_fclk"));
			break;
		case 0x3:
			omap_clk_reparent(omap_findclk(s->mpu, "omap3_sys_clkout2"),
                          omap_findclk(s->mpu, "omap3_54m_fclk"));
			break;
	}

	divor = (s->cm_clkout_ctrl&0x31)>>3;
	divor = 1<<divor;
	omap_clk_setrate(omap_findclk(s->mpu, "omap3_sys_clkout2"), divor, 1);
	
}

static void omap3_cm_reset(struct omap3_cm_s *s)
{
    s->cm_fclken_iva2 = 0x0;
    s->cm_clken_pll_iva2 = 0x11;
    s->cm_idlest_iva2 = 0x1;
    s->cm_idlest_pll_iva2 = 0x0;
    s->cm_autoidle_pll_iva2 = 0x0;
    s->cm_clksel1_pll_iva2 = 0x80000;
    s->cm_clksel2_pll_iva2 = 0x1;
    s->cm_clkstctrl_iva2 = 0x0;
    s->cm_clkstst_iva2 = 0x0;

    s->cm_revision = 0x10;
    s->cm_sysconfig = 0x1;

    s->cm_clken_pll_mpu = 0x15;
    s->cm_idlest_mpu = 0x1;
    s->cm_idlest_pll_mpu = 0x0;
    s->cm_autoidle_pll_mpu = 0x0;
    s->cm_clksel1_pll_mpu = 0x80000;
    s->cm_clksel2_pll_mpu = 0x1;
    s->cm_clkstctrl_mpu = 0x0;
    s->cm_clkstst_mpu = 0x0;

    s->cm_fclken1_core = 0x0;
    s->cm_fclken3_core = 0x0;
    s->cm_iclken1_core = 0x42;
    s->cm_iclken2_core = 0x0;
    s->cm_iclken3_core = 0x0;
    /*allow access to devices*/
    s->cm_idlest1_core = 0x0;
    s->cm_idlest2_core = 0x0;
    /*ide status =0 */
    s->cm_idlest3_core = 0xa; 
    s->cm_autoidle1_core = 0x0;
    s->cm_autoidle2_core = 0x0;
    s->cm_autoidle3_core = 0x0;
    s->cm_clksel_core = 0x105;
    s->cm_clkstctrl_core = 0x0;
    s->cm_clkstst_core = 0x0;

    s->cm_fclken_sgx = 0x0;
    s->cm_iclken_sgx = 0x0;
    s->cm_idlest_sgx = 0x1;
    s->cm_clksel_sgx = 0x0;
    s->cm_sleepdep_sgx = 0x0;
    s->cm_clkstctrl_sgx = 0x0;
    s->cm_clkstst_sgx = 0x0;

    s->cm_fclken_wkup = 0x0;
    s->cm_iclken_wkup = 0x0;
    /*assume all clock can be accessed*/
    s->cm_idlest_wkup = 0x0;
    s->cm_autoidle_wkup = 0x0;
    s->cm_clksel_wkup = 0x12;

    s->cm_clken_pll = 0x110015;
    s->cm_clken2_pll = 0x11;
    s->cm_idlest_ckgen = 0x0;
    s->cm_idlest2_ckgen = 0x0;
    s->cm_autoidle_pll = 0x0;
    s->cm_autoidle2_pll = 0x0;
    s->cm_clksel1_pll = 0x8000040;
    s->cm_clksel2_pll = 0x0;
    s->cm_clksel3_pll = 0x1;
    s->cm_clksel4_pll = 0x0;
    s->cm_clksel5_pll = 0x1;
    s->cm_clkout_ctrl = 0x3;


    s->cm_fclken_dss = 0x0;
    s->cm_iclken_dss = 0x0;
    /*dss can be accessed*/
    s->cm_idlest_dss = 0x0;
    s->cm_autoidle_dss = 0x0;
    s->cm_clksel_dss = 0x1010;
    s->cm_sleepdep_dss = 0x0;
    s->cm_clkstctrl_dss = 0x0;
    s->cm_clkstst_dss = 0x0;

    s->cm_fclken_cam = 0x0;
    s->cm_iclken_cam = 0x0;
    s->cm_idlest_cam = 0x1;
    s->cm_autoidle_cam = 0x0;
    s->cm_clksel_cam = 0x10;
    s->cm_sleepdep_cam = 0x0;
    s->cm_clkstctrl_cam = 0x0;
    s->cm_clkstst_cam = 0x0;

    s->cm_fclken_per = 0x0;
    s->cm_iclken_per = 0x0;
    //s->cm_idlest_per = 0x3ffff;
    s->cm_idlest_per = 0x0; //enable GPIO access
    s->cm_autoidle_per = 0x0;
    s->cm_clksel_per = 0x0;
    s->cm_sleepdep_per = 0x0;
    s->cm_clkstctrl_per = 0x0;
    s->cm_clkstst_per = 0x0;

    s->cm_clksel1_emu = 0x10100a50;
    s->cm_clkstctrl_emu = 0x2;
    s->cm_clkstst_emu = 0x0;
    s->cm_clksel2_emu = 0x0;
    s->cm_clksel3_emu = 0x0;

    s->cm_polctrl = 0x0;

    s->cm_idlest_neon = 0x1;
    s->cm_clkstctrl_neon = 0x0;

    s->cm_fclken_usbhost = 0x0;
    s->cm_iclken_usbhost = 0x0;
    s->cm_idlest_usbhost = 0x3;
    s->cm_autoidle_usbhost = 0x0;
    s->cm_sleepdep_usbhost = 0x0;
    s->cm_clkstctrl_usbhost = 0x0;
    s->cm_clkstst_usbhost = 0x0;
}

static uint32_t omap3_cm_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_cm_s *s = (struct omap3_cm_s *) opaque;
    uint32_t ret;
    uint32_t bypass = 0, m;

    switch (addr)
    {
    case 0x0:
    	return s->cm_fclken_iva2;
    case 0x04:
        return s->cm_clken_pll_iva2;
    case 0x20:
    	return s->cm_idlest_iva2;
    case 0x24:
        if (((s->cm_clken_pll_iva2 & 0x7) == 0x5)
            || ((s->cm_clken_pll_iva2 & 0x7) == 0x1))
        {
            bypass = 1;
        }
        else if ((s->cm_clken_pll_iva2 & 0x7) == 0x7)
        {
            m = (s->cm_clksel1_pll_iva2 & 0x7ff00) >> 8;
            if ((m == 0) || (m == 1))
                bypass = 1;
            else
                bypass = 0;
        }
        if (bypass)
            return 0;
        else
            return 1;
    case 0x34:
    	return s->cm_autoidle_pll_iva2;
    case 0x40:
        return s->cm_clksel1_pll_iva2;
    case 0x44:
        return s->cm_clksel2_pll_iva2;
    case 0x48:
    	return s->cm_clkstctrl_iva2;
    case 0x4c:
    	return s->cm_clkstst_iva2;

   case 0x800:
   		return s->cm_revision;
   	case 0x810:
   		return s->cm_sysconfig;

    	
    case 0x904:                /*CM_CLKEN_PLL_MPU */
        return s->cm_clken_pll_mpu;
   case 0x920:
   		return s->cm_idlest_mpu & 0x0;  /*MPU is active*/
    case 0x924:
        if ((s->cm_clken_pll_mpu & 0x7) == 0x5)
        {
            bypass = 1;
        }
        else if ((s->cm_clken_pll_mpu & 0x7) == 0x7)
        {
            m = (s->cm_clksel1_pll_mpu & 0x7ff00) >> 8;
            if ((m == 0) || (m == 1))
                bypass = 1;
            else
                bypass = 0;
        }
        if (bypass)
            return 0;
        else
            return 1;
    case 0x934:
    	return s->cm_autoidle_pll_mpu;
    case 0x940:
        return s->cm_clksel1_pll_mpu;
    case 0x944:
        return s->cm_clksel2_pll_mpu;
     case 0x948:
     	return s->cm_clkstctrl_mpu;
     case 0x94c:
     	return s->cm_clkstst_mpu;


     	
    case 0xa00:
        return s->cm_fclken1_core;
    case 0xa08:
    	return s->cm_fclken3_core;
    case 0xa10:
        return s->cm_iclken1_core;
    case 0xa14:
    	 return s->cm_iclken2_core;
    case 0xa20:
    	return s->cm_idlest1_core;
    case 0xa24:
    	return s->cm_idlest2_core;
    case 0xa28:
    	return s->cm_idlest3_core;
    case 0xa30:
    	return s->cm_autoidle1_core;
    case 0xa34:
    	return s->cm_autoidle2_core;
    case 0xa38:
    	return s->cm_autoidle3_core;
    case 0xa40:                /*CM_CLKSEL_CORE */
        return s->cm_clksel_core;
    case 0xa48:
    	 return s->cm_clkstctrl_core;
     case 0xa4c:
     	return s->cm_clkstst_core;

   case 0xb00:
   		return s->cm_fclken_sgx;
   	case 0xb10:
   		return s->cm_iclken_sgx;
   	case 0xb20:
   		return s->cm_idlest_sgx&0x0;
   case 0xb40:                /*CM_CLKSEL_SGX */
        return s->cm_clksel_sgx;
   case 0xb48:
   		return s->cm_clkstctrl_sgx;
   	case 0xb4c:
   		return s->cm_clkstst_sgx;

   		
    case 0xc00:                /*CM_FCLKEN_WKUP */
        return s->cm_fclken_wkup;
    case 0xc10:                /*CM_ICLKEN_WKUP */
        return s->cm_iclken_wkup;
    case 0xc20:                /*CM_IDLEST_WKUP */
        /*TODO: Check whether the timer can be accessed. */
        return 0x0;
    case 0xc30:
    	return s->cm_idlest_wkup;
    case 0xc40:
        return s->cm_clksel_wkup;
    case 0xc48:
    	return s->cm_c48;

    	
    case 0xd00:                /*CM_CLKEN_PLL */
        return s->cm_clken_pll;
    case 0xd04:
    	return s->cm_clken2_pll;
    case 0xd20:
    	 /*FIXME: all clock is active. we do not care it. */
        ret = 0x3ffff;

    	/*DPLL3*/
    	bypass = 0;
    	if (((s->cm_clken_pll & 0x7) == 0x5) || ((s->cm_clken_pll & 0x7) == 0x6))
	        bypass = 1;
        else if ((s->cm_clken_pll & 0x7) == 0x7) {
            m = (s->cm_clksel1_pll & 0x7ff0000) >> 16;
            if ((m == 0) || (m == 1))
                bypass = 1;
            else
                bypass = 0;
        }
        if (bypass)
            ret &= 0xfffe;
        
        /*DPLL4*/
	    bypass = 0;
	    if ((s->cm_clken_pll & 0x70000) == 0x10000)
            bypass = 1;
        else if ((s->cm_clken_pll & 0x70000) == 0x70000) {
            m = (s->cm_clksel2_pll & 0x7ff00) >> 8;
            if ((m == 0) || (m == 1))
                bypass = 1;
            else
                bypass = 0;
        }
        if (bypass)
            ret &= 0xfffd;
    	return ret;
    	
    case 0xd24:
    	return s->cm_idlest2_ckgen;
    case 0xd30:
    	return s->cm_autoidle_pll;
    case 0xd34:
    	return s->cm_autoidle2_pll;
    case 0xd40:                /*CM_CLKSEL1_PLL */
        return s->cm_clksel1_pll;
    case 0xd44:
        return s->cm_clksel2_pll;
    case 0xd48:                /*CM_CLKSEL3_PLL */
        return s->cm_clksel3_pll;
    case 0xd4c:
        return s->cm_clksel4_pll;
    case 0xd50:                /*CM_CLKSEL5_PLL */
        return s->cm_clksel5_pll;
    case 0xd70:
    	 return s->cm_clkout_ctrl;

    	 
    case 0xe00:
    	return s->cm_fclken_dss;
   	case 0xe10:
    	return s->cm_iclken_dss;
    case 0xe20:
    	return s->cm_idlest_dss;
    case 0xe30:
    	return s->cm_autoidle_dss;
    case 0xe40:
        return s->cm_clksel_dss;
    case 0xe44:
        return s->cm_sleepdep_dss;
    case 0xe48:
        return s->cm_clkstctrl_dss;
    case 0xe4c:
        return s->cm_clkstst_dss;

        
    case 0xf00:
    	return s->cm_fclken_cam;
    case 0xf10:
    	return s->cm_iclken_cam;
    case 0xf20:
    	return s->cm_idlest_cam&0x0;
    case 0xf30:
    	return s->cm_autoidle_cam;
    case 0xf40:
        return s->cm_clksel_cam;
    case 0xf44:
    	return s->cm_sleepdep_cam;
    case 0xf48:
    	return s->cm_clkstctrl_cam;
    case 0xf4c:
    	return s->cm_clkstst_cam;

    	
    case 0x1000:
        return s->cm_fclken_per;
    case 0x1010:
        return s->cm_iclken_per;
    case 0x1020:
    	return s->cm_idlest_per ;
    case 0x1030:
    	return s->cm_autoidle_per;
    case 0x1040:
        return s->cm_clksel_per;
    case 0x1044:
    	return s->cm_sleepdep_per;
    case 0x1048:
    	return s->cm_clkstctrl_per;
    case 0x104c:
		return s->cm_clkstst_per;

    	
    case 0x1140:               /*CM_CLKSEL1_EMU */
        return s->cm_clksel1_emu;
    case 0x1148:
    	 return s->cm_clkstctrl_emu;
    case 0x114c:
    	return s->cm_clkstst_emu&0x0;
    case 0x1150:
    	return s->cm_clksel2_emu;
    case 0x1154:
    	return s->cm_clksel3_emu;

   case 0x129c:
   		return s->cm_polctrl;

   	case 0x1320:
   		return s->cm_idlest_neon&0x0;
   	case 0x1348:
   		return s->cm_clkstctrl_neon;

   	case 0x1400:
   		return s->cm_fclken_usbhost;
   	case 0x1410:
   		return s->cm_iclken_usbhost;
   	case 0x1420:
   		return s->cm_idlest_usbhost&0x0;
    case 0x1430:
    	return s->cm_autoidle_usbhost;
    case 0x1444:
    	return s->cm_sleepdep_usbhost;
    case 0x1448:
    	return s->cm_clkstctrl_usbhost;
    case 0x144c:
    	return s->cm_clkstst_usbhost;

    default:
        printf("omap3_cm_read addr %x pc %x \n", addr, cpu_single_env->regs[15] );
        exit(-1);
    }
}


static void omap3_cm_write(void *opaque, target_phys_addr_t addr,
                           uint32_t value)
{
    struct omap3_cm_s *s = (struct omap3_cm_s *) opaque;

    switch (addr)
    {
    case 0x20:
    case 0x24:
    case 0x4c:
    case 0x800:
    case 0x920:
    case 0x924:
    case 0x94c:
    case 0xa20:
    case 0xa24:
    case 0xa28:
    case 0xa4c:
    case 0xb20:
    case 0xb4c:
    case 0xc20:                /*CM_IDLEST_WKUP */
    case 0xd20:
    case 0xd24:
    case 0xe20:
    case 0xe4c:
    case 0xf20:
    case 0xf4c:
    case 0x1020:
    case 0x104c:
    case 0x114c:
    case 0x1320:
    case 0x1420:
    case 0x144c:
        OMAP_RO_REG(addr);
        exit(-1);
        break;
        
    case 0x0:
    	s->cm_fclken_iva2 = value & 0x1;
    	break;
    case 0x4:                  /*CM_CLKEN_PLL_IVA2 */
        s->cm_clken_pll_iva2 = value & 0x7ff;
        omap3_cm_iva2_update(s);
        break;
    case 0x34:
    	s->cm_autoidle_pll_iva2 = value & 0x7;
    	break;
    case 0x40:
        s->cm_clksel1_pll_iva2 = value & 0x3fff7f;
        //printf("value %x s->cm_clksel1_pll_iva2 %x \n",value,s->cm_clksel1_pll_iva2);
        omap3_cm_iva2_update(s);
        break;
    case 0x44:
        s->cm_clksel2_pll_iva2 = value & 0x1f;
        omap3_cm_iva2_update(s);
        break;
    case 0x48:
    	s->cm_clkstctrl_iva2 = value& 0x3;
    	break;

    case 0x810:
    	s->cm_sysconfig = value & 0x1;
    	break;

        
    case 0x904:                /*CM_CLKEN_PLL_MPU */
        s->cm_clken_pll_mpu = value & 0x7ff;
        omap3_cm_mpu_update(s);
        break;
    case 0x934:
    	s->cm_autoidle_pll_mpu = value & 0x7;
    	break;
    case 0x940:
        //printf("s->cm_clksel1_pll_mpu  %x\n",s->cm_clksel1_pll_mpu );
        s->cm_clksel1_pll_mpu = value & 0x3fff7f;
        omap3_cm_mpu_update(s);
        break;
    case 0x944:
        s->cm_clksel2_pll_mpu = value & 0x1f;
        omap3_cm_mpu_update(s);
        break;
    case 0x948:
    	s->cm_clkstctrl_mpu = value & 0x3;
    	break;

    	
    case 0xa00:
        s->cm_fclken1_core = value & 0x43fffe00;
         break;
    case 0xa08:
    	 s->cm_fclken3_core = value & 0x7;
    	 break;
    case 0xa10:
        s->cm_iclken1_core = value & 0x7ffffed2;
         break;
    case 0xa14:
    	 s->cm_iclken2_core = value & 0x1f;
    	 break;
    case 0xa18:
    	s->cm_iclken3_core = value & 0x2;
    	break;
    case 0xa30:
    	s->cm_autoidle1_core = value & 0x7ffffed0;
    	break;
    case 0xa34:
    	s->cm_autoidle2_core = value & 0x1f;
    	break;
    case 0xa38:
    	s->cm_autoidle3_core = value & 0x2;
    	break;
    case 0xa40:                /*CM_CLKSEL_CORE */
        s->cm_clksel_core = (value & 0xff);
        s->cm_clksel_core |= 0x100;
        omap3_cm_gp10_update(s);
        omap3_cm_gp11_update(s);
        omap3_cm_l3clk_update(s);
        omap3_cm_l4clk_update(s);
        break;
    case 0xa48:
    	s->cm_clkstctrl_core = value & 0xf;
    	break;

    case 0xb00:
    	s->cm_fclken_sgx = value &0x2;
    	break;
    case 0xb10:
    	s->cm_iclken_sgx = value & 0x1;
    	break;
    case 0xb40:                /*CM_CLKSEL_SGX */
        /*TODO: SGX Clock!! */
        s->cm_clksel_sgx = value;
        break;
    case 0xb44:
    	s->cm_sleepdep_sgx = value &0x2;
    	break;
    case 0xb48:
    	s->cm_clkstctrl_sgx = value & 0x3;
    	break;

    
    case 0xc00:                /*CM_FCLKEN_WKUP */
        s->cm_fclken_wkup = value & 0x2e9;
        break;
    case 0xc10:                /*CM_ICLKEN_WKUP */
        s->cm_iclken_wkup = value & 0x2ff;
        break;
    case 0xc30:
    	s->cm_autoidle_wkup = value & 0x23f;
    	break;
    case 0xc40:                /*CM_CLKSEL_WKUP */
        s->cm_clksel_wkup = value & 0x7f;
        omap3_cm_clksel_wkup_update(s, s->cm_clksel_wkup);
        break;

        
    case 0xd00:                /*CM_CLKEN_PLL */
        s->cm_clken_pll = value & 0xffff17ff;
        omap3_cm_dpll3_update(s);
        omap3_cm_dpll4_update(s);
        break;
    case 0xd04:
    	s->cm_clken2_pll = value & 0x7ff;
    	break;
    case 0xd30:
    	s->cm_autoidle_pll = value & 0x3f;
    	break;
    case 0xd34:
    	s->cm_autoidle2_pll = value & 0x7;
    	break;
    case 0xd40:                /*CM_CLKSEL1_PLL */
        //OMAP3_DEBUG(("WD40 value %x \n",value));
        s->cm_clksel1_pll = value & 0xffffbffc;
        //OMAP3_DEBUG(("WD40 value %x \n",value));
        omap3_cm_dpll3_update(s);
        omap3_cm_48m_update(s);
        break;
    case 0xd44:
        s->cm_clksel2_pll = value & 0x7ff7f;
        omap3_cm_dpll4_update(s);
        break;
    case 0xd48:                /*CM_CLKSEL3_PLL */
        s->cm_clksel3_pll = value & 0x1f;
        omap3_cm_dpll4_update(s);
        break;
    case 0xd4c:                /*CM_CLKSEL4_PLL */  
      	s->cm_clksel4_pll = value & 0x7ff7f;
        omap3_cm_dpll5_update(s);
        break;
     case 0xd50:                /*CM_CLKSEL5_PLL */
        s->cm_clksel5_pll = value & 0x1f;
        omap3_cm_dpll5_update(s);
        break;
    case 0xd70:
    	s->cm_clkout_ctrl = value & 0xbb;
    	omap3_cm_clkout2_update(s);
    	break;
        
    case 0xe00:
    	s->cm_fclken_dss = value & 0x7;
    	break;
   	case 0xe10:
    	s->cm_iclken_dss = value & 0x1;
    	break;
    case 0xe30:
    	s->cm_autoidle_dss = value & 0x1;
    	break;
    case 0xe40:
        s->cm_clksel_dss = value & 0x1f1f;
        omap3_cm_dpll4_update(s);
        break;
   case 0xe44:
   		s->cm_sleepdep_dss = value & 0x7;
       break;
   case 0xe48:
   		s->cm_clkstctrl_dss = value & 0x3;
       break;
        
    case 0xf00:
    	s->cm_fclken_cam = value & 0x3;
    	break;
    case 0xf10:
    	s->cm_iclken_cam = value & 0x1;
    	break;
    case 0xf30:
    	s->cm_autoidle_cam = value & 0x1;
    	break;
    case 0xf40:
        s->cm_clksel_cam = value & 0x1f;
        omap3_cm_dpll4_update(s);
        break;
    case 0xf44:
    	s->cm_sleepdep_cam = value & 0x2;
    	break;
    case 0xf48:
    	s->cm_clkstctrl_cam = value & 0x3;
    	break;
   
    case 0x1000:
        s->cm_fclken_per = value & 0x3ffff;
        break;
    case 0x1010:
        s->cm_iclken_per = value & 0x3ffff;
        break;
    
    case 0x1030:
    	s->cm_autoidle_per = value &0x3ffff;
    	break;
    case 0x1040:
        s->cm_clksel_per = value & 0xff;
        omap3_cm_per_gptimer_update(s);
        break;
    case 0x1044:
    	s->cm_sleepdep_per = value & 0x6;
    	break;
    case 0x1048:
    	 s->cm_clkstctrl_per = value &0x7;
    	 break;
    	 
    case 0x1140:               /*CM_CLKSEL1_EMU */
        s->cm_clksel1_emu = value & 0x1f1f3fff;
        //printf("cm_clksel1_emu %x\n",s->cm_clksel1_emu);
        omap3_cm_dpll3_update(s);
        omap3_cm_dpll4_update(s);
        break;
    case 0x1148:
    	s->cm_clkstctrl_emu = value & 0x3;
    	break;
	 case 0x1150:
	 	 s->cm_clksel2_emu = value & 0xfff7f;
	 	 omap3_cm_dpll3_update(s);
        break;
    case 0x1154:
    	 s->cm_clksel3_emu = value & 0xfff7f;
	 	 omap3_cm_dpll4_update(s);
        break;

    case 0x129c:
    	 s->cm_polctrl = value & 0x1;
    	 break;

   case 0x1348:
   		s->cm_clkstctrl_neon = value & 0x3;
   		break;

   	case 0x1400:
   		s->cm_fclken_usbhost = value & 0x3;
   		break;
   	case 0x1410:
   		s->cm_iclken_usbhost = value & 0x1;
   		break;
    case 0x1430:
    	s->cm_autoidle_usbhost = value & 0x1;
    	break;
    case 0x1444:
    	s->cm_sleepdep_usbhost = value & 0x6;
    	break;
    case 0x1448:
    	s->cm_clkstctrl_usbhost = value & 0x3;
    	break;
   
    default:
        printf("omap3_cm_write addr %x value %x pc %x\n", addr, value,cpu_single_env->regs[15] );
        exit(-1);
    }
}



static CPUReadMemoryFunc *omap3_cm_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_cm_read,
};

static CPUWriteMemoryFunc *omap3_cm_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_cm_write,
};

struct omap3_cm_s *omap3_cm_init(struct omap_target_agent_s *ta,
                                 qemu_irq mpu_int, qemu_irq dsp_int,
                                 qemu_irq iva_int, struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_cm_s *s = (struct omap3_cm_s *) qemu_mallocz(sizeof(*s));

    s->irq[0] = mpu_int;
    s->irq[1] = dsp_int;
    s->irq[2] = iva_int;
    s->mpu = mpu;
    omap3_cm_reset(s);

    iomemtype = l4_register_io_memory(0, omap3_cm_readfn, omap3_cm_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    omap_l4_attach(ta, 1, iomemtype);

    return s;
}

#define OMAP3_SEC_WDT          1
#define OMAP3_MPU_WDT         2
#define OMAP3_IVA2_WDT        3
/*omap3 watchdog timer*/
struct omap3_wdt_s
{
    qemu_irq irq;               /*IVA2 IRQ */
    struct omap_mpu_state_s *mpu;
    omap_clk clk;
    QEMUTimer *timer;

    int active;
    int64_t rate;
    int64_t time;
    //int64_t ticks_per_sec;

    uint32_t wd_sysconfig;
    uint32_t wd_sysstatus;
    uint32_t wisr;
    uint32_t wier;
    uint32_t wclr;
    uint32_t wcrr;
    uint32_t wldr;
    uint32_t wtgr;
    uint32_t wwps;
    uint32_t wspr;

    /*pre and ptv in wclr */
    uint32_t pre;
    uint32_t ptv;
    //uint32_t val;

    uint16_t writeh;            /* LSB */
    uint16_t readh;             /* MSB */

};





static inline void omap3_wdt_timer_update(struct omap3_wdt_s *wdt_timer)
{
    int64_t expires;
    if (wdt_timer->active)
    {
        expires = muldiv64(0xffffffffll - wdt_timer->wcrr,
                           ticks_per_sec, wdt_timer->rate);
        qemu_mod_timer(wdt_timer->timer, wdt_timer->time + expires);
    }
    else
        qemu_del_timer(wdt_timer->timer);
}
static void omap3_wdt_clk_setup(struct omap3_wdt_s *timer)
{
    /*TODO: Add irq as user to clk */
}

static inline uint32_t omap3_wdt_timer_read(struct omap3_wdt_s *timer)
{
    uint64_t distance;

    if (timer->active)
    {
        distance = qemu_get_clock(vm_clock) - timer->time;
        distance = muldiv64(distance, timer->rate, ticks_per_sec);

        if (distance >= 0xffffffff - timer->wcrr)
            return 0xffffffff;
        else
            return timer->wcrr + distance;
    }
    else
        return timer->wcrr;
}

/*
static inline void omap3_wdt_timer_sync(struct omap3_wdt_s *timer)
{
    if (timer->active) {
        timer->val = omap3_wdt_timer_read(timer);
        timer->time = qemu_get_clock(vm_clock);
    }
}*/

static void omap3_wdt_reset(struct omap3_wdt_s *s, int wdt_index)
{
    s->wd_sysconfig = 0x0;
    s->wd_sysstatus = 0x0;
    s->wisr = 0x0;
    s->wier = 0x0;
    s->wclr = 0x20;
    s->wcrr = 0x0;
    switch (wdt_index)
    {
    case OMAP3_MPU_WDT:
    case OMAP3_IVA2_WDT:
        s->wldr = 0xfffb0000;
        break;
    case OMAP3_SEC_WDT:
        s->wldr = 0xffa60000;
        break;
    }
    s->wtgr = 0x0;
    s->wwps = 0x0;
    s->wspr = 0x0;

    switch (wdt_index)
    {
    case OMAP3_SEC_WDT:
    case OMAP3_MPU_WDT:
        s->active = 1;
        break;
    case OMAP3_IVA2_WDT:
        s->active = 0;
        break;
    }
    s->pre = s->wclr & (1 << 5);
    s->ptv = (s->wclr & 0x1c) >> 2;
    s->rate = omap_clk_getrate(s->clk) >> (s->pre ? s->ptv : 0);

    s->active = 1;
    s->time = qemu_get_clock(vm_clock);
    omap3_wdt_timer_update(s);
}

static uint32_t omap3_wdt_read32(void *opaque, target_phys_addr_t addr,
                                 int wdt_index)
{
    struct omap3_wdt_s *s = (struct omap3_wdt_s *) opaque;

    //uint32_t ret;
    //printf("omap3_wdt_read32 addr %x \n",addr);
    switch (addr)
    {
    case 0x10:                 /*WD_SYSCONFIG */
        return s->wd_sysconfig;
    case 0x14:                 /*WD_SYSSTATUS */
        return s->wd_sysstatus;
    case 0x18:
         /*WISR*/ return s->wisr & 0x1;
    case 0x1c:
         /*WIER*/ return s->wier & 0x1;
    case 0x24:
         /*WCLR*/ return s->wclr & 0x3c;
    case 0x28:
         /*WCRR*/ s->wcrr = omap3_wdt_timer_read(s);
        s->time = qemu_get_clock(vm_clock);
        return s->wcrr;
    case 0x2c:
         /*WLDR*/ return s->wldr;
    case 0x30:
         /*WTGR*/ return s->wtgr;
    case 0x34:
         /*WWPS*/ return s->wwps;
    case 0x48:
         /*WSPR*/ return s->wspr;
    default:
        printf("omap3_wdt_read32 addr %x \n", addr);
        exit(-1);
    }
}
static uint32_t omap3_mpu_wdt_read16(void *opaque, target_phys_addr_t addr)
{
    struct omap3_wdt_s *s = (struct omap3_wdt_s *) opaque;
    uint32_t ret;

    if (addr & 2)
        return s->readh;
    else
    {
        ret = omap3_wdt_read32(opaque, addr, OMAP3_MPU_WDT);
        s->readh = ret >> 16;
        return ret & 0xffff;
    }
}
static uint32_t omap3_mpu_wdt_read32(void *opaque, target_phys_addr_t addr)
{
    return omap3_wdt_read32(opaque, addr, OMAP3_MPU_WDT);
}

static void omap3_wdt_write32(void *opaque, target_phys_addr_t addr,
                              uint32_t value, int wdt_index)
{
    struct omap3_wdt_s *s = (struct omap3_wdt_s *) opaque;

    //printf("omap3_wdt_write32 addr %x value %x \n",addr,value);
    switch (addr)
    {
    case 0x14:                 /*WD_SYSSTATUS */
    case 0x34:
         /*WWPS*/ OMAP_RO_REG(addr);
        exit(-1);
        break;
    case 0x10:                 /*WD_SYSCONFIG */
        s->wd_sysconfig = value & 0x33f;
        break;
    case 0x18:
         /*WISR*/ s->wisr = value & 0x1;
        break;
    case 0x1c:
         /*WIER*/ s->wier = value & 0x1;
        break;
    case 0x24:
         /*WCLR*/ s->wclr = value & 0x3c;
        break;
    case 0x28:
         /*WCRR*/ s->wcrr = value;
        s->time = qemu_get_clock(vm_clock);
        omap3_wdt_timer_update(s);
        break;
    case 0x2c:
         /*WLDR*/ s->wldr = value;      /*It will take effect after next overflow */
        break;
    case 0x30:
         /*WTGR*/ if (value != s->wtgr)
        {
            s->wcrr = s->wldr;
            s->pre = s->wclr & (1 << 5);
            s->ptv = (s->wclr & 0x1c) >> 2;
            s->rate = omap_clk_getrate(s->clk) >> (s->pre ? s->ptv : 0);
            s->time = qemu_get_clock(vm_clock);
            omap3_wdt_timer_update(s);
        }
        s->wtgr = value;
        break;
    case 0x48:
         /*WSPR*/
            if (((value & 0xffff) == 0x5555) && ((s->wspr & 0xffff) == 0xaaaa))
        {
            s->active = 0;
            s->wcrr = omap3_wdt_timer_read(s);
            omap3_wdt_timer_update(s);
        }
        if (((value & 0xffff) == 0x4444) && ((s->wspr & 0xffff) == 0xbbbb))
        {
            s->active = 1;
            s->time = qemu_get_clock(vm_clock);
            omap3_wdt_timer_update(s);
        }
        s->wspr = value;
        break;
    default:
        printf("omap3_wdt_write32 addr %x \n", addr);
        exit(-1);
    }
}

static void omap3_mpu_wdt_write16(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    struct omap3_wdt_s *s = (struct omap3_wdt_s *) opaque;

    if (addr & 2)
        return omap3_wdt_write32(opaque, addr, (value << 16) | s->writeh,
                                 OMAP3_MPU_WDT);
    else
        s->writeh = (uint16_t) value;
}
static void omap3_mpu_wdt_write32(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    omap3_wdt_write32(opaque, addr, value, OMAP3_MPU_WDT);
}


static CPUReadMemoryFunc *omap3_mpu_wdt_readfn[] = {
    omap_badwidth_read32,
    omap3_mpu_wdt_read16,
    omap3_mpu_wdt_read32,
};

static CPUWriteMemoryFunc *omap3_mpu_wdt_writefn[] = {
    omap_badwidth_write32,
    omap3_mpu_wdt_write16,
    omap3_mpu_wdt_write32,
};



static void omap3_mpu_wdt_timer_tick(void *opaque)
{
    struct omap3_wdt_s *wdt_timer = (struct omap3_wdt_s *) opaque;

    /*TODO:Sent reset pulse to PRCM */
    wdt_timer->wcrr = wdt_timer->wldr;

    /*after overflow, generate the new wdt_timer->rate */
    wdt_timer->pre = wdt_timer->wclr & (1 << 5);
    wdt_timer->ptv = (wdt_timer->wclr & 0x1c) >> 2;
    wdt_timer->rate =
        omap_clk_getrate(wdt_timer->clk) >> (wdt_timer->pre ? wdt_timer->
                                             ptv : 0);

    wdt_timer->time = qemu_get_clock(vm_clock);
    omap3_wdt_timer_update(wdt_timer);
}

static struct omap3_wdt_s *omap3_mpu_wdt_init(struct omap_target_agent_s *ta,
                                              qemu_irq irq, omap_clk fclk,
                                              omap_clk iclk,
                                              struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_wdt_s *s = (struct omap3_wdt_s *) qemu_mallocz(sizeof(*s));

    s->irq = irq;
    s->clk = fclk;
    s->timer = qemu_new_timer(vm_clock, omap3_mpu_wdt_timer_tick, s);

    omap3_wdt_reset(s, OMAP3_MPU_WDT);
    if (irq != NULL)
        omap3_wdt_clk_setup(s);

    iomemtype = l4_register_io_memory(0, omap3_mpu_wdt_readfn,
                                      omap3_mpu_wdt_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;

}


/*dummy system control module*/
struct omap3_scm_s
{
    struct omap_mpu_state_s *mpu;

	uint8 interface[48];           /*0x4800 2000*/
	uint8 padconfs[576];         /*0x4800 2030*/
	uint32 general[228];            /*0x4800 2270*/
	uint8 mem_wkup[1024];     /*0x4800 2600*/
	uint8 padconfs_wkup[84]; /*0x4800 2a00*/
	uint32 general_wkup[8];    /*0x4800 2a60*/
};

#define PADCONFS_VALUE(wakeup0,wakeup1,offmode0,offmode1, \
						inputenable0,inputenable1,pupd0,pupd1,muxmode0,muxmode1,offset) \
	do { \
		 *(padconfs+offset/4) = (wakeup0 <<14)|(offmode0<<9)|(inputenable0<<8)|(pupd0<<3)|(muxmode0); \
		 *(padconfs+offset/4) |= (wakeup1 <<30)|(offmode1<<25)|(inputenable1<<24)|(pupd1<<19)|(muxmode1<<16); \
} while (0)


static void omap3_scm_reset(struct omap3_scm_s *s)
{
	 uint32 * padconfs;
    padconfs = (uint32 *)(s->padconfs);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x0);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x4);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x8);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0xc);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x10);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x14);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x18);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x1c);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x20);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x24);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x28);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x2c);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x30);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x34);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x38);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x3c);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x40);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x44);
    PADCONFS_VALUE(0,0,0,0,1,1,0,1,0,7,0x48);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x4c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x50);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x54);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x58);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,0,0x5c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x60);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x64);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x68);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x6c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x70);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x74);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x78);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x7c);
    PADCONFS_VALUE(0,0,0,0,1,1,0,3,0,7,0x80);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x84);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x88);
    PADCONFS_VALUE(0,0,0,0,1,1,3,0,7,0,0x8c);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x90);
    PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x94);
    PADCONFS_VALUE(0,0,0,0,1,1,1,0,7,0,0x98);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,7,0x9c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0xa0);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0xa4);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0xa8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xac);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xb0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xb4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xb8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xbc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xc0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xc4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xc8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xcc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xd0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xd4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xd8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xdc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xe0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xe4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xe8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xec);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xf0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xf4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xf8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0xfc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x100);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x104);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x108);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x10c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x110);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x114);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x118);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x11c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x120);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x124);
    PADCONFS_VALUE(0,0,0,0,1,1,1,3,7,7,0x128);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x12c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x130);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x134);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x138);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x13c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x140);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x144);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x148);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x14c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x150);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x154);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x158);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x15c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x160);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x164);
    PADCONFS_VALUE(0,0,0,0,1,1,1,3,7,7,0x168);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x16c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0x170);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0x174);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x178);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x17c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x180);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x184);
    PADCONFS_VALUE(0,0,0,0,1,1,1,3,7,7,0x188);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x18c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x190);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x194);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x198);
    PADCONFS_VALUE(0,0,0,0,1,1,1,3,7,7,0x19c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x1a0);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0x1a4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x1a8);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0x1ac);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,7,7,0x1b0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1b4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1b8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1bc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1c0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1c4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1c8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1cc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1d0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1d4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1d8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1dc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1e0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1e4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1e8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1ec);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1f0);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1f4);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1f8);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1fc);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x200);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x204);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x208);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x20c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x210);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x214);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x218);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x21c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x220);
    PADCONFS_VALUE(0,0,0,0,1,1,3,1,0,0,0x224);
    PADCONFS_VALUE(0,0,0,0,1,1,0,1,0,0,0x228);
    PADCONFS_VALUE(0,0,0,0,1,1,0,1,0,0,0x22c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x230);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,7,7,0x234);


	padconfs = (uint32 *)(s->general);
	s->general[1] = 0x4000000;  /*0x4800 2274*/
	s->general[0x1c] = 0x1;  /*0x4800 22e0*/
	s->general[0x75] = 0x7fc0;  /*0x4800 2444*/
	s->general[0x76] = 0xaa;  /*0x4800 2448*/
	s->general[0x7c] = 0x2700;  /*0x4800 2460*/
	s->general[0x7d] = 0x300000;  /*0x4800 2464*/
	s->general[0x7e] = 0x300000;  /*0x4800 2468*/
	s->general[0x81] = 0xffff;  /*0x4800 2474*/
	s->general[0x82] = 0xffff;  /*0x4800 2478*/
	s->general[0x83] = 0xffff;  /*0x4800 247c*/
	s->general[0x84] = 0x6;  /*0x4800 2480*/
	s->general[0x85] = 0xffffffff;  /*0x4800 2484*/
	s->general[0x86] = 0xffff;  /*0x4800 2488*/
	s->general[0x87] = 0xffff;  /*0x4800 248c*/
	s->general[0x88] = 0x1;  /*0x4800 2490*/
	s->general[0x8b] = 0xffffffff;  /*0x4800 249c*/
	s->general[0x8c] = 0xffff;  /*0x4800 24a0*/
	s->general[0x8e] = 0xffff;  /*0x4800 24a8*/
	s->general[0x8f] = 0xffff;  /*0x4800 24ac*/
	s->general[0x91] = 0xffff;  /*0x4800 24b4*/
	s->general[0x92] = 0xffff;  /*0x4800 24b8*/
	s->general[0xac] = 0x109;  /*0x4800 2520*/
	s->general[0xb2] = 0xffff;  /*0x4800 2538*/
	s->general[0xb3] = 0xffff;  /*0x4800 253c*/
	s->general[0xb4] = 0xffff;  /*0x4800 2540*/
	PADCONFS_VALUE(0,0,0,0,1,1,3,3,4,4,0x368);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,4,4,0x36c);
    PADCONFS_VALUE(0,0,0,0,1,1,3,3,4,4,0x370);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x374);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x378);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x37c);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x380);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x384);
    PADCONFS_VALUE(0,0,0,0,1,1,1,1,4,4,0x388);

    

	padconfs = (uint32 *)(s->padconfs_wkup);
	PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x0);
	PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x4);
	PADCONFS_VALUE(0,0,0,0,1,1,3,0,0,0,0x8);
	PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0xc);
	PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x10);
	PADCONFS_VALUE(0,0,0,0,1,1,0,0,0,0,0x14);
	PADCONFS_VALUE(0,0,0,0,1,1,1,1,7,7,0x18);
	PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x1c);
	PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x20);
	PADCONFS_VALUE(0,0,0,0,1,1,3,3,0,0,0x24);
	PADCONFS_VALUE(0,0,0,0,1,1,1,1,0,0,0x2c);


	s->general_wkup[0] = 0x66ff; /*0x4800 2A60*/
	    
}

static uint32_t omap3_scm_read8(void *opaque, target_phys_addr_t addr)
{
    struct omap3_scm_s *s = (struct omap3_scm_s *) opaque;
    uint8_t* temp;
	
    switch (addr) {
    case 0x00 ... 0x2f:
        return s->interface[addr];
    case 0x30 ... 0x26f:
        return s->padconfs[addr-0x30];
    case 0x270 ... 0x5ff:
        temp = (uint8_t *)s->general;
        return temp[addr-0x270];
    case 0x600 ... 0x9ff:
        return s->mem_wkup[addr-0x600];
    case 0xa00 ... 0xa5f:
        return s->padconfs_wkup[addr-0xa00];
    case 0xa60 ... 0xa7f:
        temp = (uint8_t *)s->general_wkup;
        return temp[addr-0xa60];
    /* case 0x2f0:
        return s->control_status & 0xff;
    case 0x2f1:
        return (s->control_status & 0xff00) >> 8;
    case 0x2f2:
        return (s->control_status & 0xff0000) >> 16;
    case 0x2f3:
        return (s->control_status & 0xff000000) >> 24;    */
	
    default:
        break;
    }
    printf("omap3_scm_read8 addr %x pc %x  \n", addr,cpu_single_env->regs[15] );
    return 0;
}

static uint32_t omap3_scm_read16(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = omap3_scm_read8(opaque, addr);
    v |= omap3_scm_read8(opaque, addr + 1) << 8;
    return v;
}

static uint32_t omap3_scm_read32(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = omap3_scm_read8(opaque, addr);
    v |= omap3_scm_read8(opaque, addr + 1) << 8;
    v |= omap3_scm_read8(opaque, addr + 2) << 16;
    v |= omap3_scm_read8(opaque, addr + 3) << 24;
    return v;
}

static void omap3_scm_write8(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    struct omap3_scm_s *s = (struct omap3_scm_s *) opaque;
    uint8_t* temp;

    switch (addr)
    {
    case 0x00 ... 0x2f:
        s->interface[addr] = value;
        break;
    case 0x30 ... 0x26f:
        s->padconfs[addr-0x30] = value;
        break;
    case 0x270 ... 0x5ff:
        temp = (uint8_t *)s->general;
        temp[addr-0x270] = value;
        break;
    case 0x600 ... 0x9ff:
        s->mem_wkup[addr-0x600] = value;
        break;
    case 0xa00 ... 0xa5f:
        s->padconfs_wkup[addr-0xa00] = value;
        break;
    case 0xa60 ... 0xa7f:
        temp = (uint8_t *)s->general_wkup;
        temp[addr-0xa60] = value;
        break;
    default:
        /*we do not care scm write*/
        printf("omap3_scm_write8 addr %x pc %x \n \n", addr,
               cpu_single_env->regs[15] - 0x80008000 + 0x80e80000);
        exit(1);
        //break;
    }
}

static void omap3_scm_write16(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    omap3_scm_write8(opaque, addr + 0, (value) & 0xff);
    omap3_scm_write8(opaque, addr + 1, (value >> 8) & 0xff);
}

static void omap3_scm_write32(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    omap3_scm_write8(opaque, addr + 0, (value) & 0xff);
    omap3_scm_write8(opaque, addr + 1, (value >> 8) & 0xff);
    omap3_scm_write8(opaque, addr + 2, (value >> 16) & 0xff);
    omap3_scm_write8(opaque, addr + 3, (value >> 24) & 0xff);
}

static CPUReadMemoryFunc *omap3_scm_readfn[] = {
    omap3_scm_read8,
    omap3_scm_read16,
    omap3_scm_read32,
};

static CPUWriteMemoryFunc *omap3_scm_writefn[] = {
    omap3_scm_write8,
    omap3_scm_write16,
    omap3_scm_write32,
};

static struct omap3_scm_s *omap3_scm_init(struct omap_target_agent_s *ta,
                                          struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_scm_s *s = (struct omap3_scm_s *) qemu_mallocz(sizeof(*s));

    s->mpu = mpu;

    omap3_scm_reset(s);

    iomemtype = l4_register_io_memory(0, omap3_scm_readfn,
                                      omap3_scm_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    
    return s;
}


/*dummy port protection*/
struct omap3_pm_s
{
    struct omap_mpu_state_s *mpu;

    uint32_t l3_pm_rt_error_log;        /*0x6801 0020 */
    uint32_t l3_pm_rt_control;  /*0x6801 0028 */
    uint32_t l3_pm_rt_error_clear_single;       /*0x6801 0030 */
    uint32_t l3_pm_rt_error_clear_multi;        /*0x6801 0038 */
    uint32_t l3_pm_rt_req_info_permission[2];   /*0x6801 0048 + (0x20*i) */
    uint32_t l3_pm_rt_read_permission[2];       /*0x6801 0050 + (0x20*i) */
    uint32_t l3_pm_rt_write_permission[2];      /*0x6801 0058 + (0x20*i) */
    uint32_t l3_pm_rt_addr_match[1];    /*0x6801 0060 + (0x20*k) */

    uint32_t l3_pm_gpmc_error_log;      /*0x6801 2420 */
    uint32_t l3_pm_gpmc_control;        /*0x6801 2428 */
    uint32_t l3_pm_gpmc_error_clear_single;     /*0x6801 2430 */
    uint32_t l3_pm_gpmc_error_clear_multi;      /*0x6801 2438 */
    uint32_t l3_pm_gpmc_req_info_permission[8]; /*0x6801 2448 + (0x20*i) */
    uint32_t l3_pm_gpmc_read_permission[8];     /*0x6801 2450 + (0x20*i) */
    uint32_t l3_pm_gpmc_write_permission[8];    /*0x6801 2458 + (0x20*i) */
    uint32_t l3_pm_gpmc_addr_match[7];  /*0x6801 2460 + (0x20*k) */

    uint32_t l3_pm_ocmram_error_log;    /*0x6801 2820 */
    uint32_t l3_pm_ocmram_control;      /*0x6801 2828 */
    uint32_t l3_pm_ocmram_error_clear_single;   /*0x6801 2830 */
    uint32_t l3_pm_ocmram_error_clear_multi;    /*0x6801 2838 */
    uint32_t l3_pm_ocmram_req_info_permission[8];       /*0x6801 2848 + (0x20*i) */
    uint32_t l3_pm_ocmram_read_permission[8];   /*0x6801 2850 + (0x20*i) */
    uint32_t l3_pm_ocmram_write_permission[8];  /*0x6801 2858 + (0x20*i) */
    uint32_t l3_pm_ocmram_addr_match[7];        /*0x6801 2860 + (0x20*k) */

    uint32_t l3_pm_ocmrom_error_log;    /*0x6801 2c20 */
    uint32_t l3_pm_ocmrom_control;      /*0x6801 2c28 */
    uint32_t l3_pm_ocmrom_error_clear_single;   /*0x6801 2c30 */
    uint32_t l3_pm_ocmrom_error_clear_multi;    /*0x6801 2c38 */
    uint32_t l3_pm_ocmrom_req_info_permission[2];       /*0x6801 2c48 + (0x20*i) */
    uint32_t l3_pm_ocmrom_read_permission[2];   /*0x6801 2c50 + (0x20*i) */
    uint32_t l3_pm_ocmrom_write_permission[2];  /*0x6801 2c58 + (0x20*i) */
    uint32_t l3_pm_ocmrom_addr_match[1];        /*0x6801 2c60 + (0x20*k) */

    uint32_t l3_pm_mad2d_error_log;     /*0x6801 3020 */
    uint32_t l3_pm_mad2d_control;       /*0x6801 3028 */
    uint32_t l3_pm_mad2d_error_clear_single;    /*0x6801 3030 */
    uint32_t l3_pm_mad2d_error_clear_multi;     /*0x6801 3038 */
    uint32_t l3_pm_mad2d_req_info_permission[8];        /*0x6801 3048 + (0x20*i) */
    uint32_t l3_pm_mad2d_read_permission[8];    /*0x6801 3050 + (0x20*i) */
    uint32_t l3_pm_mad2d_write_permission[8];   /*0x6801 3058 + (0x20*i) */
    uint32_t l3_pm_mad2d_addr_match[7]; /*0x6801 3060 + (0x20*k) */

    uint32_t l3_pm_iva_error_log;       /*0x6801 4020 */
    uint32_t l3_pm_iva_control; /*0x6801 4028 */
    uint32_t l3_pm_iva_error_clear_single;      /*0x6801 4030 */
    uint32_t l3_pm_iva_error_clear_multi;       /*0x6801 4038 */
    uint32_t l3_pm_iva_req_info_permission[4];  /*0x6801 4048 + (0x20*i) */
    uint32_t l3_pm_iva_read_permission[4];      /*0x6801 4050 + (0x20*i) */
    uint32_t l3_pm_iva_write_permission[4];     /*0x6801 4058 + (0x20*i) */
    uint32_t l3_pm_iva_addr_match[3];   /*0x6801 4060 + (0x20*k) */
};

static void omap3_pm_reset(struct omap3_pm_s *s)
{
    int i;

    s->l3_pm_rt_control = 0x3000000;
    s->l3_pm_gpmc_control = 0x3000000;
    s->l3_pm_ocmram_control = 0x3000000;
    s->l3_pm_ocmrom_control = 0x3000000;
    s->l3_pm_mad2d_control = 0x3000000;
    s->l3_pm_iva_control = 0x3000000;

    s->l3_pm_rt_req_info_permission[0] = 0xffff;
    s->l3_pm_rt_req_info_permission[1] = 0x0;
    for (i = 3; i < 8; i++)
        s->l3_pm_gpmc_req_info_permission[i] = 0xffff;
    for (i = 1; i < 8; i++)
        s->l3_pm_ocmram_req_info_permission[i] = 0xffff;
    s->l3_pm_ocmrom_req_info_permission[1] = 0xffff;
    for (i = 1; i < 8; i++)
        s->l3_pm_mad2d_req_info_permission[i] = 0xffff;
    for (i = 1; i < 4; i++)
        s->l3_pm_iva_req_info_permission[i] = 0xffff;

    s->l3_pm_rt_read_permission[0] = 0x1406;
    s->l3_pm_rt_read_permission[1] = 0x1406;
    s->l3_pm_rt_write_permission[0] = 0x1406;
    s->l3_pm_rt_write_permission[1] = 0x1406;
    for (i = 0; i < 8; i++)
    {
        s->l3_pm_gpmc_read_permission[i] = 0x563e;
        s->l3_pm_gpmc_write_permission[i] = 0x563e;
    }
    for (i = 0; i < 8; i++)
    {
        s->l3_pm_ocmram_read_permission[i] = 0x5f3e;
        s->l3_pm_ocmram_write_permission[i] = 0x5f3e;
    }
    for (i = 0; i < 2; i++)
    {
        s->l3_pm_ocmrom_read_permission[i] = 0x1002;
        s->l3_pm_ocmrom_write_permission[i] = 0x1002;
    }

    for (i = 0; i < 8; i++)
    {
        s->l3_pm_mad2d_read_permission[i] = 0x5f1e;
        s->l3_pm_mad2d_write_permission[i] = 0x5f1e;
    }

    for (i = 0; i < 4; i++)
    {
        s->l3_pm_iva_read_permission[i] = 0x140e;
        s->l3_pm_iva_write_permission[i] = 0x140e;
    }


    s->l3_pm_rt_addr_match[0] = 0x10230;

    s->l3_pm_gpmc_addr_match[0] = 0x10230;
}

static uint32_t omap3_pm_read8(void *opaque, target_phys_addr_t addr)
{
    //struct omap3_pm_s *s = (struct omap3_pm_s *) opaque;

    switch (addr)
    {
    default:
        printf("omap3_pm_read8 addr %x \n", addr);
        exit(-1);
    }
}

static uint32_t omap3_pm_read16(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = omap3_pm_read8(opaque, addr);
    v |= omap3_pm_read8(opaque, addr + 1) << 8;
    return v;
}

static uint32_t omap3_pm_read32(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = omap3_pm_read8(opaque, addr);
    v |= omap3_pm_read8(opaque, addr + 1) << 8;
    v |= omap3_pm_read8(opaque, addr + 2) << 16;
    v |= omap3_pm_read8(opaque, addr + 3) << 24;
    return v;
}

static void omap3_pm_write8(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    struct omap3_pm_s *s = (struct omap3_pm_s *) opaque;
    int i;

    switch (addr)
    {
    case 0x48 ... 0x4b:
    case 0x68 ... 0x6b:
        i = (addr - 0x48) / 0x20;
        s->l3_pm_rt_req_info_permission[i] &=
            (~(0xff << ((addr - 0x48 - i * 0x20) * 8)));
        s->l3_pm_rt_req_info_permission[i] |=
            (value << (addr - 0x48 - i * 0x20) * 8);
        break;
    case 0x50 ... 0x53:
    case 0x70 ... 0x73:
        i = (addr - 0x50) / 0x20;
        s->l3_pm_rt_read_permission[i] &=
            (~(0xff << ((addr - 0x50 - i * 0x20) * 8)));
        s->l3_pm_rt_read_permission[i] |=
            (value << (addr - 0x50 - i * 0x20) * 8);
        break;
    case 0x58 ... 0x5b:
    case 0x78 ... 0x7b:
        i = (addr - 0x58) / 0x20;
        s->l3_pm_rt_write_permission[i] &=
            (~(0xff << ((addr - 0x58 - i * 0x20) * 8)));
        s->l3_pm_rt_write_permission[i] |=
            (value << (addr - 0x58 - i * 0x20) * 8);
        break;
    case 0x60 ... 0x63:
        s->l3_pm_rt_addr_match[0] &= (~(0xff << ((addr - 0x60) * 8)));
        s->l3_pm_rt_addr_match[0] |= (value << (addr - 0x60) * 8);
        break;
    case 0x2448 ... 0x244b:
    case 0x2468 ... 0x246b:
    case 0x2488 ... 0x248b:
    case 0x24a8 ... 0x24ab:
    case 0x24c8 ... 0x24cb:
    case 0x24e8 ... 0x24eb:
    case 0x2508 ... 0x250b:
    case 0x2528 ... 0x252b:
        i = (addr - 0x2448) / 0x20;
        s->l3_pm_gpmc_req_info_permission[i] &=
            (~(0xff << ((addr - 0x2448 - i * 0x20) * 8)));
        s->l3_pm_gpmc_req_info_permission[i] |=
            (value << (addr - 0x2448 - i * 0x20) * 8);
        break;
    case 0x2450 ... 0x2453:
    case 0x2470 ... 0x2473:
    case 0x2490 ... 0x2493:
    case 0x24b0 ... 0x24b3:
    case 0x24d0 ... 0x24d3:
    case 0x24f0 ... 0x24f3:
    case 0x2510 ... 0x2513:
    case 0x2530 ... 0x2533:
        i = (addr - 0x2450) / 0x20;
        s->l3_pm_gpmc_read_permission[i] &=
            (~(0xff << ((addr - 0x2450 - i * 0x20) * 8)));
        s->l3_pm_gpmc_read_permission[i] |=
            (value << (addr - 0x2450 - i * 0x20) * 8);
        break;
    case 0x2458 ... 0x245b:
    case 0x2478 ... 0x247b:
    case 0x2498 ... 0x249b:
    case 0x24b8 ... 0x24bb:
    case 0x24d8 ... 0x24db:
    case 0x24f8 ... 0x24fb:
    case 0x2518 ... 0x251b:
    case 0x2538 ... 0x253b:
        i = (addr - 0x2458) / 0x20;
        s->l3_pm_gpmc_write_permission[i] &=
            (~(0xff << ((addr - 0x2458 - i * 0x20) * 8)));
        s->l3_pm_gpmc_write_permission[i] |=
            (value << (addr - 0x2458 - i * 0x20) * 8);
        break;
    case 0x2848 ... 0x284b:
    case 0x2868 ... 0x286b:
    case 0x2888 ... 0x288b:
    case 0x28a8 ... 0x28ab:
    case 0x28c8 ... 0x28cb:
    case 0x28e8 ... 0x28eb:
    case 0x2908 ... 0x290b:
    case 0x2928 ... 0x292b:
        i = (addr - 0x2848) / 0x20;
        s->l3_pm_ocmram_req_info_permission[i] &=
            (~(0xff << ((addr - 0x2848 - i * 0x20) * 8)));
        s->l3_pm_ocmram_req_info_permission[i] |=
            (value << (addr - 0x2848 - i * 0x20) * 8);
        break;
    case 0x2850 ... 0x2853:
    case 0x2870 ... 0x2873:
    case 0x2890 ... 0x2893:
    case 0x28b0 ... 0x28b3:
    case 0x28d0 ... 0x28d3:
    case 0x28f0 ... 0x28f3:
    case 0x2910 ... 0x2913:
    case 0x2930 ... 0x2933:
        i = (addr - 0x2850) / 0x20;
        s->l3_pm_ocmram_read_permission[i] &=
            (~(0xff << ((addr - 0x2850 - i * 0x20) * 8)));
        s->l3_pm_ocmram_read_permission[i] |=
            (value << (addr - 0x2850 - i * 0x20) * 8);
        break;
    case 0x2858 ... 0x285b:
    case 0x2878 ... 0x287b:
    case 0x2898 ... 0x289b:
    case 0x28b8 ... 0x28bb:
    case 0x28d8 ... 0x28db:
    case 0x28f8 ... 0x28fb:
    case 0x2918 ... 0x291b:
    case 0x2938 ... 0x293b:
        i = (addr - 0x2858) / 0x20;
        s->l3_pm_ocmram_write_permission[i] &=
            (~(0xff << ((addr - 0x2858 - i * 0x20) * 8)));
        s->l3_pm_ocmram_write_permission[i] |=
            (value << (addr - 0x2858 - i * 0x20) * 8);
        break;

    case 0x2860 ... 0x2863:
    case 0x2880 ... 0x2883:
    case 0x28a0 ... 0x28a3:
    case 0x28c0 ... 0x28c3:
    case 0x28e0 ... 0x28e3:
    case 0x2900 ... 0x2903:
    case 0x2920 ... 0x2923:
        i = (addr - 0x2860) / 0x20;
        s->l3_pm_ocmram_addr_match[i] &=
            (~(0xff << ((addr - 0x2860 - i * 0x20) * 8)));
        s->l3_pm_ocmram_addr_match[i] |=
            (value << (addr - 0x2860 - i * 0x20) * 8);
        break;

    case 0x4048 ... 0x404b:
    case 0x4068 ... 0x406b:
    case 0x4088 ... 0x408b:
    case 0x40a8 ... 0x40ab:
        i = (addr - 0x4048) / 0x20;
        s->l3_pm_iva_req_info_permission[i] &=
            (~(0xff << ((addr - 0x4048 - i * 0x20) * 8)));
        s->l3_pm_iva_req_info_permission[i] |=
            (value << (addr - 0x4048 - i * 0x20) * 8);
        break;
    case 0x4050 ... 0x4053:
    case 0x4070 ... 0x4073:
    case 0x4090 ... 0x4093:
    case 0x40b0 ... 0x40b3:
        i = (addr - 0x4050) / 0x20;
        s->l3_pm_iva_read_permission[i] &=
            (~(0xff << ((addr - 0x4050 - i * 0x20) * 8)));
        s->l3_pm_iva_read_permission[i] |=
            (value << (addr - 0x4050 - i * 0x20) * 8);
        break;
    case 0x4058 ... 0x405b:
    case 0x4078 ... 0x407b:
    case 0x4098 ... 0x409b:
    case 0x40b8 ... 0x40bb:
        i = (addr - 0x4058) / 0x20;
        s->l3_pm_iva_write_permission[i] &=
            (~(0xff << ((addr - 0x4058 - i * 0x20) * 8)));
        s->l3_pm_iva_write_permission[i] |=
            (value << (addr - 0x4058 - i * 0x20) * 8);
        break;
    default:
        printf("omap3_pm_write8 addr %x \n", addr);
        exit(-1);
    }
}

static void omap3_pm_write16(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    omap3_pm_write8(opaque, addr + 0, (value) & 0xff);
    omap3_pm_write8(opaque, addr + 1, (value >> 8) & 0xff);
}

static void omap3_pm_write32(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    omap3_pm_write8(opaque, addr + 0, (value) & 0xff);
    omap3_pm_write8(opaque, addr + 1, (value >> 8) & 0xff);
    omap3_pm_write8(opaque, addr + 2, (value >> 16) & 0xff);
    omap3_pm_write8(opaque, addr + 3, (value >> 24) & 0xff);
}

static CPUReadMemoryFunc *omap3_pm_readfn[] = {
    omap3_pm_read8,
    omap3_pm_read16,
    omap3_pm_read32,
};

static CPUWriteMemoryFunc *omap3_pm_writefn[] = {
    omap3_pm_write8,
    omap3_pm_write16,
    omap3_pm_write32,
};

static struct omap3_pm_s *omap3_pm_init(struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_pm_s *s = (struct omap3_pm_s *) qemu_mallocz(sizeof(*s));

    s->mpu = mpu;
    //s->base = 0x68010000;
    //s->size = 0x4400;

    omap3_pm_reset(s);

    iomemtype = cpu_register_io_memory(0, omap3_pm_readfn, omap3_pm_writefn, s);
    cpu_register_physical_memory(0x68010000, 0x4400, iomemtype);

    return s;
}

/*dummy SDRAM Memory Scheduler emulation*/
struct omap3_sms_s
{
    struct omap_mpu_state_s *mpu;

    uint32 sms_sysconfig;
    uint32 sms_sysstatus;
    uint32 sms_rg_att[8];
    uint32 sms_rg_rdperm[8];
    uint32 sms_rg_wrperm[8];
    uint32 sms_rg_start[7];
    uint32 sms_rg_end[7];
    uint32 sms_security_control;
    uint32 sms_class_arbiter0;
    uint32 sms_class_arbiter1;
    uint32 sms_class_arbiter2;
    uint32 sms_interclass_arbiter;
    uint32 sms_class_rotation[3];
    uint32 sms_err_addr;
    uint32 sms_err_type;
    uint32 sms_pow_ctrl;
    uint32 sms_rot_control[12];
    uint32 sms_rot_size[12];
    uint32 sms_rot_physical_ba[12];


};

static uint32_t omap3_sms_read32(void *opaque, target_phys_addr_t addr)
{
    struct omap3_sms_s *s = (struct omap3_sms_s *) opaque;

    switch (addr)
    {
    case 0x10:
    	return s->sms_sysconfig;
    case 0x14:
    	return s->sms_sysstatus;
    case 0x48:
    case 0x68:
    case 0x88:
    case 0xa8:
    case 0xc8:
    case 0xe8:
    case 0x108:
    case 0x128:
    	return s->sms_rg_att[(addr-0x48)/0x20];
    case 0x50:
    case 0x70:
    case 0x90:
    case 0xb0:
    case 0xd0:
    case 0xf0:
    case 0x110:
    case 0x130:
    	return s->sms_rg_rdperm[(addr-0x50)/0x20];
    case 0x58:
    case 0x78:
    case 0x98:
    case 0xb8:
    case 0xd8:
    case 0xf8:
    case 0x118:
    	return s->sms_rg_wrperm[(addr-0x58)/0x20];
    case 0x60:
    case 0x80:
    case 0xa0:
    case 0xc0:
    case 0xe0:
    case 0x100:
    case 0x120:
    	return s->sms_rg_start[(addr-0x60)/0x20];

    case 0x64:
    case 0x84:
    case 0xa4:
    case 0xc4:
    case 0xe4:
    case 0x104:
    case 0x124:
    	return s->sms_rg_end[(addr-0x64)/0x20];
    case 0x140:
    	return s->sms_security_control;
    case 0x150:
    	return s->sms_class_arbiter0;
	case 0x154:
		return s->sms_class_arbiter1;
	case 0x158:
		return s->sms_class_arbiter2;
	case 0x160:
		return s->sms_interclass_arbiter;
	case 0x164:
	case 0x168:
	case 0x16c:
		return s->sms_class_rotation[(addr-0x164)/4];
	case 0x170:
		return s->sms_err_addr;
	case 0x174:
		return s->sms_err_type;
	case 0x178:
		return s->sms_pow_ctrl;
	case 0x180:
	case 0x190:
	case 0x1a0:
	case 0x1b0:
	case 0x1c0:
	case 0x1d0:
	case 0x1e0:
	case 0x1f0:
	case 0x200:
	case 0x210:
	case 0x220:
	case 0x230:
		return s->sms_rot_control[(addr-0x180)/0x10];
	case 0x184:
	case 0x194:
	case 0x1a4:
	case 0x1b4:
	case 0x1c4:
	case 0x1d4:
	case 0x1e4:
	case 0x1f4:
	case 0x204:
	case 0x214:
	case 0x224:
	case 0x234:
		return s->sms_rot_size[(addr-0x184)/0x10];

	case 0x188:
	case 0x198:
	case 0x1a8:
	case 0x1b8:
	case 0x1c8:
	case 0x1d8:
	case 0x1e8:
	case 0x1f8:
	case 0x208:
	case 0x218:
	case 0x228:
	case 0x238:
		return s->sms_rot_size[(addr-0x188)/0x10];

    default:
        printf("omap3_sms_read32 addr %x \n", addr);
        exit(-1);
    }
}

static void omap3_sms_write32(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    struct omap3_sms_s *s = (struct omap3_sms_s *) opaque;
    //int i;

    switch (addr)
    {
    case 0x14:
    	OMAP_RO_REG(addr);
        return;
    case 0x10:
    	s->sms_sysconfig = value & 0x1f;
    	break;
    
    case 0x48:
    case 0x68:
    case 0x88:
    case 0xa8:
    case 0xc8:
    case 0xe8:
    case 0x108:
    case 0x128:
    	s->sms_rg_att[(addr-0x48)/0x20] = value;
    	break;
    case 0x50:
    case 0x70:
    case 0x90:
    case 0xb0:
    case 0xd0:
    case 0xf0:
    case 0x110:
    case 0x130:
    	s->sms_rg_rdperm[(addr-0x50)/0x20] = value&0xffff;
    	break;
    case 0x58:
    case 0x78:
    case 0x98:
    case 0xb8:
    case 0xd8:
    case 0xf8:
    case 0x118:
    	s->sms_rg_wrperm[(addr-0x58)/0x20] = value&0xffff;
    	break;    	
    case 0x60:
    case 0x80:
    case 0xa0:
    case 0xc0:
    case 0xe0:
    case 0x100:
    case 0x120:
    	s->sms_rg_start[(addr-0x60)/0x20] = value;
    	break;
    case 0x64:
    case 0x84:
    case 0xa4:
    case 0xc4:
    case 0xe4:
    case 0x104:
    case 0x124:
    	s->sms_rg_end[(addr-0x64)/0x20] = value;
    	break;
    case 0x140:
    	s->sms_security_control = value &0xfffffff;
    	break;
    case 0x150:
    	s->sms_class_arbiter0 = value;
    	break;
	case 0x154:
		s->sms_class_arbiter1 = value;
		break;
	case 0x158:
		s->sms_class_arbiter2 = value;
		break;
	case 0x160:
		s->sms_interclass_arbiter = value;
		break;
	case 0x164:
	case 0x168:
	case 0x16c:
		s->sms_class_rotation[(addr-0x164)/4] = value;
		break;
	case 0x170:
		s->sms_err_addr = value;
		break;
	case 0x174:
		s->sms_err_type = value;
		break;
	case 0x178:
		s->sms_pow_ctrl = value;
		break;
	case 0x180:
	case 0x190:
	case 0x1a0:
	case 0x1b0:
	case 0x1c0:
	case 0x1d0:
	case 0x1e0:
	case 0x1f0:
	case 0x200:
	case 0x210:
	case 0x220:
	case 0x230:
		s->sms_rot_control[(addr-0x180)/0x10] = value;
		break;
	case 0x184:
	case 0x194:
	case 0x1a4:
	case 0x1b4:
	case 0x1c4:
	case 0x1d4:
	case 0x1e4:
	case 0x1f4:
	case 0x204:
	case 0x214:
	case 0x224:
	case 0x234:
		s->sms_rot_size[(addr-0x184)/0x10] = value;
		break;

	case 0x188:
	case 0x198:
	case 0x1a8:
	case 0x1b8:
	case 0x1c8:
	case 0x1d8:
	case 0x1e8:
	case 0x1f8:
	case 0x208:
	case 0x218:
	case 0x228:
	case 0x238:
		s->sms_rot_size[(addr-0x188)/0x10] = value;   
		break;
	default:
        printf("omap3_sms_write32 addr %x\n", addr);
        exit(-1);
    }
}

static CPUReadMemoryFunc *omap3_sms_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_sms_read32,
};

static CPUWriteMemoryFunc *omap3_sms_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_sms_write32,
};

static void omap3_sms_reset(struct omap3_sms_s *s)
{
	s->sms_sysconfig = 0x1;
	s->sms_class_arbiter0 = 0x500000;
	s->sms_class_arbiter1 = 0x500;
	s->sms_class_arbiter2 = 0x55000;
	s->sms_interclass_arbiter = 0x400040;
	s->sms_class_rotation[0] = 0x1;
	s->sms_class_rotation[1] = 0x1;
	s->sms_class_rotation[2] = 0x1;
	s->sms_pow_ctrl = 0x80;
}

static struct omap3_sms_s *omap3_sms_init(struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap3_sms_s *s = (struct omap3_sms_s *) qemu_mallocz(sizeof(*s));

    s->mpu = mpu;

    omap3_sms_reset(s);
    
    iomemtype = cpu_register_io_memory(0, omap3_sms_readfn,
                                       omap3_sms_writefn, s);
    cpu_register_physical_memory(0x6c000000, 0x10000, iomemtype);

    return s;
}

static const struct dma_irq_map omap3_dma_irq_map[] = {
    {0, OMAP_INT_35XX_SDMA_IRQ0},
    {0, OMAP_INT_35XX_SDMA_IRQ1},
    {0, OMAP_INT_35XX_SDMA_IRQ2},
    {0, OMAP_INT_35XX_SDMA_IRQ3},
};

static int omap3_validate_addr(struct omap_mpu_state_s *s,
                               target_phys_addr_t addr)
{
    return 1;
}

/*
  set the kind of memory connected to GPMC that we are trying to boot form.
  Uses SYS BOOT settings.
*/
void omap3_set_mem_type(struct omap_mpu_state_s *s,int bootfrom)
{
	switch (bootfrom)
	{
		case 0x0: /*GPMC_NOR*/
			s->omap3_scm->general[32] |= 7;
			break;
		case 0x1: /*GPMC_NAND*/
			s->omap3_scm->general[32] |= 1;
			break;
		case 0x2:
			s->omap3_scm->general[32] |= 8;
			break;
		case 0x3:
			s->omap3_scm->general[32] |= 0;
			break;
		case 0x4:
			s->omap3_scm->general[32] |= 17;
			break;
		case 0x5:
			s->omap3_scm->general[32] |= 3;
			break;
	}
}

void omap3_set_device_type(struct omap_mpu_state_s *s,int device_type)
{
	s->omap3_scm->general[32] |= (device_type & 0x7) << 8;
}

struct omap_mpu_state_s *omap3530_mpu_init(unsigned long sdram_size,
                                           DisplayState * ds, const char *core)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *)
        qemu_mallocz(sizeof(struct omap_mpu_state_s));
    ram_addr_t sram_base, q2_base;
    qemu_irq *cpu_irq;
    qemu_irq dma_irqs[4];
    int i;
    int sdindex;
    //omap_clk gpio_clks[4];


    s->mpu_model = omap3530;
    s->env = cpu_init("cortex-a8");
    if (!s->env)
    {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    s->sdram_size = sdram_size;
    s->sram_size = OMAP3530_SRAM_SIZE;

    sdindex = drive_get_index(IF_SD, 0, 0);
    printf("sdindex %d \n",sdindex);
    if (sdindex == -1) {
        fprintf(stderr, "qemu: missing SecureDigital device\n");
        exit(1);
    }

    /* Clocks */
    omap_clk_init(s);

    /* Memory-mapped stuff */

    q2_base = qemu_ram_alloc(s->sdram_size);
    cpu_register_physical_memory(OMAP3_Q2_BASE, s->sdram_size,
                                 (q2_base | IO_MEM_RAM));
    sram_base = qemu_ram_alloc(s->sram_size);
    cpu_register_physical_memory(OMAP3_SRAM_BASE, s->sram_size,
                                 (sram_base | IO_MEM_RAM));


    

    s->l4 = omap_l4_init(OMAP3_L4_BASE, sizeof(omap3_l4_agent_info));

    cpu_irq = arm_pic_init_cpu(s->env);
    s->ih[0] = omap2_inth_init(s,
                               0x48200000, 0x1000, 3, &s->irq[0],
                               cpu_irq[ARM_PIC_CPU_IRQ],
                               cpu_irq[ARM_PIC_CPU_FIQ], omap_findclk(s,
                                                                      "omap3_mpu_intc_fclk"),
                               omap_findclk(s, "omap3_mpu_intc_iclk"));

    for (i = 0; i < 4; i++)
        dma_irqs[i] =
            s->irq[omap3_dma_irq_map[i].ih][omap3_dma_irq_map[i].intr];
    s->dma = omap_dma4_init(0x48056000, dma_irqs, s, 256, 32,
                            omap_findclk(s, "omap3_sdma_fclk"),
                            omap_findclk(s, "omap3_sdma_iclk"));
    s->port->addr_valid = omap3_validate_addr;


    /* Register SDRAM and SRAM ports for fast DMA transfers.  */
    soc_dma_port_add_mem_ram(s->dma, q2_base, OMAP2_Q2_BASE, s->sdram_size);
    soc_dma_port_add_mem_ram(s->dma, sram_base, OMAP2_SRAM_BASE, s->sram_size);


    s->omap3_cm = omap3_cm_init(omap3_l4ta_get(s->l4, 1), NULL, NULL, NULL, s);

    s->omap3_prm = omap3_prm_init(omap3_l4ta_get(s->l4, 2),
                                  NULL, NULL, NULL, s);

    s->omap3_mpu_wdt = omap3_mpu_wdt_init(omap3_l4ta_get(s->l4, 3),
                                          NULL,
                                          omap_findclk(s,
                                                       "omap3_wkup_32k_fclk"),
                                          omap_findclk(s, "omap3_wkup_l4_iclk"),
                                          s);

    s->omap3_scm = omap3_scm_init(omap3_l4ta_get(s->l4, 4), s);

    s->omap3_pm = omap3_pm_init(s);
    s->omap3_sms = omap3_sms_init(s);

    s->gptimer[0] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 5),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER1],
                                       omap_findclk(s, "omap3_gp1_fclk"),
                                       omap_findclk(s, "omap3_wkup_l4_iclk"));
    s->gptimer[1] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 6),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER2],
                                       omap_findclk(s, "omap3_gp2_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[2] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 7),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER3],
                                       omap_findclk(s, "omap3_gp3_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[3] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 8),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER4],
                                       omap_findclk(s, "omap3_gp4_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[4] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 9),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER5],
                                       omap_findclk(s, "omap3_gp5_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[5] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 10),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER6],
                                       omap_findclk(s, "omap3_gp6_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[6] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 11),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER7],
                                       omap_findclk(s, "omap3_gp7_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[7] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 12),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER8],
                                       omap_findclk(s, "omap3_gp8_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[8] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 13),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER9],
                                       omap_findclk(s, "omap3_gp9_fclk"),
                                       omap_findclk(s, "omap3_per_l4_iclk"));
    s->gptimer[9] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 14),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER10],
                                       omap_findclk(s, "omap3_gp10_fclk"),
                                       omap_findclk(s, "omap3_core_l4_iclk"));
    s->gptimer[10] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 15),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER11],
                                       omap_findclk(s, "omap3_gp12_fclk"),
                                       omap_findclk(s, "omap3_core_l4_iclk"));
    s->gptimer[11] = omap_gp_timer_init(omap3_l4ta_get(s->l4, 16),
                                       s->irq[0][OMAP_INT_35XX_GPTIMER12],
                                       omap_findclk(s, "omap3_gp12_fclk"),
                                       omap_findclk(s, "omap3_wkup_l4_iclk"));

    
	
    omap_synctimer_init(omap3_l4ta_get(s->l4, 17), s,
                        omap_findclk(s, "omap3_sys_32k"), NULL);

    s->sdrc = omap_sdrc_init(0x6d000000);
    
    s->gpmc = omap_gpmc_init(s, 0x6e000000, s->irq[0][OMAP_INT_35XX_GPMC_IRQ]);
    

    s->uart[0] = omap2_uart_init(omap3_l4ta_get(s->l4, 18),
                                 s->irq[0][OMAP_INT_35XX_UART1_IRQ],
                                 omap_findclk(s, "omap3_uart1_fclk"),
                                 omap_findclk(s, "omap3_uart1_iclk"),
                                 s->drq[OMAP35XX_DMA_UART1_TX],
                                 s->drq[OMAP35XX_DMA_UART1_RX], serial_hds[0]);
    s->uart[1] = omap2_uart_init(omap3_l4ta_get(s->l4, 19),
                                 s->irq[0][OMAP_INT_35XX_UART2_IRQ],
                                 omap_findclk(s, "omap3_uart2_fclk"),
                                 omap_findclk(s, "omap3_uart2_iclk"),
                                 s->drq[OMAP35XX_DMA_UART2_TX],
                                 s->drq[OMAP35XX_DMA_UART2_RX],
                                 serial_hds[0] ? serial_hds[1] : 0);
    s->uart[2] = omap2_uart_init(omap3_l4ta_get(s->l4, 20),
                                 s->irq[0][OMAP_INT_35XX_UART3_IRQ],
                                 omap_findclk(s, "omap3_uart2_fclk"),
                                 omap_findclk(s, "omap3_uart3_iclk"),
                                 s->drq[OMAP35XX_DMA_UART3_TX],
                                 s->drq[OMAP35XX_DMA_UART3_RX],
                                 serial_hds[0]
                                 && serial_hds[1] ? serial_hds[2] : 0);
    
    /*attach serial[0] to uart 2 for beagle board */
    omap_uart_attach(s->uart[2], serial_hds[0]);

    s->dss = omap_dss_init(omap3_l4ta_get(s->l4, 21), 0x68005400, ds,
                    s->irq[0][OMAP_INT_35XX_DSS_IRQ], s->drq[OMAP24XX_DMA_DSS],
                   NULL,NULL,NULL,NULL,NULL);

    //gpio_clks[0] = NULL;
    //gpio_clks[1] = NULL;
    //gpio_clks[2] = NULL;
    //gpio_clks[3] = NULL;

    s->gpif = omap3_gpif_init();
    /*gpio 1*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 22),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK1], 
                    NULL,NULL,0);
    /*gpio 2*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 23),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK2], 
                    NULL,NULL,1);
    /*gpio 3*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 24),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK3], 
                    NULL,NULL,2);
    /*gpio 4*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 25),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK4], 
                    NULL,NULL,3);

    /*gpio 5*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 26),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK5], 
                    NULL,NULL,4);
     /*gpio 6*/
    omap3_gpio_init(s, s->gpif ,omap3_l4ta_get(s->l4, 27),
                    &s->irq[0][OMAP_INT_35XX_GPIO_BANK6], 
                    NULL,NULL,5);

     omap_tap_init(omap3_l4ta_get(s->l4, 28), s);

    s->mmc = omap3_mmc_init(omap3_l4ta_get(s->l4, 29), drives_table[sdindex].bdrv,
                    s->irq[0][OMAP_INT_35XX_MMC1_IRQ],
                    &s->drq[OMAP35XX_DMA_MMC1_TX],
                    omap_findclk(s, "omap3_mmc1_fclk"), omap_findclk(s, "omap3_mmc1_iclk"));

    s->i2c[0] = omap2_i2c_init(omap3_l4ta_get(s->l4, 32),
                    s->irq[0][OMAP_INT_35XX_I2C1_IRQ],
                    &s->drq[OMAP35XX_DMA_I2C1_TX],
                    omap_findclk(s, "omap3_i2c1_fclk"),
                    omap_findclk(s, "omap3_i2c1_iclk"));
    s->i2c[1] = omap2_i2c_init(omap3_l4ta_get(s->l4, 33),
                    s->irq[0][OMAP_INT_35XX_I2C2_IRQ],
                    &s->drq[OMAP35XX_DMA_I2C2_TX],
                    omap_findclk(s, "omap3_i2c2_fclk"),
                    omap_findclk(s, "omap3_i2c2_iclk"));
    s->i2c[2] = omap2_i2c_init(omap3_l4ta_get(s->l4, 34),
                    s->irq[0][OMAP_INT_35XX_I2C3_IRQ],
                    &s->drq[OMAP35XX_DMA_I2C3_TX],
                    omap_findclk(s, "omap3_i2c3_fclk"),
                    omap_findclk(s, "omap3_i2c3_iclk"));

    return s;
}
