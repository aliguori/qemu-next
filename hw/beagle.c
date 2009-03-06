/*
 * Beagle board emulation. http://beagleboard.org/
 * 
 * Copyright (C) 2008 yajin(yajin@vm-kernel.org)
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

#include "qemu-common.h"
#include "sysemu.h"
#include "omap.h"
#include "arm-misc.h"
#include "irq.h"
#include "console.h"
#include "boards.h"
#include "i2c.h"
#include "devices.h"
#include "flash.h"
#include "hw.h"
#include "block.h"

#define BEAGLE_NAND_CS			0

#define GPMC_NOR             0
#define GPMC_NAND           1
#define GPMC_MDOC           2
#define GPMC_ONENAND    3
#define MMC_NAND            4
#define MMC_ONENAND     5


#define TST_DEVICE              0x0
#define EMU_DEVICE              0x1
#define HS_DEVICE               0x2
#define GP_DEVICE               0x3

#define DEBUG_BEAGLE

#ifdef DEBUG_BEAGLE
#define BEAGLE_DEBUG(...)    do { fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define BEAGLE_DEBUG(x)    
#endif

/* Beagle board support */
struct beagle_s {
    struct omap_mpu_state_s *cpu;
    
    struct nand_flash_s *nand;
    struct omap3_lcd_panel_s *lcd_panel;
    i2c_bus *i2c;
    struct twl4030_s *twl4030;
};



static struct arm_boot_info beagle_binfo = {
    .ram_size = 0x08000000,
};


static void beagle_nand_setup(struct beagle_s *s)
{
	s->nand = nand_init(NAND_MFR_MICRON, 0xba); /* MT29F2G16ABC */
	nand_setpins(s->nand, 0, 0, 0, 1, 0); /* no write-protect */
    omap_gpmc_attach(s->cpu->gpmc, BEAGLE_NAND_CS, 0, NULL, NULL, s, s->nand);
    omap3_set_mem_type(s->cpu, GPMC_NAND);
}

static int beagle_nand_read_page(struct beagle_s *s,uint8_t *buf, uint32_t addr)
{
	uint16_t *p = (uint16_t *)buf;
	int i;

    /* send command: reset */
    nand_setpins(s->nand, 1, 0, 0, 1, 0);
    nand_setio(s->nand, 0xff);
	/* send command: read page (cycle1) */
    nand_setpins(s->nand, 1, 0, 0, 1, 0);
    nand_setio(s->nand, 0);
	/* send page address (x16 device):
       bits  0-11 define cache address in words (bit11 set only for OOB access)
       bits 16-33 define page and block address */
    nand_setpins(s->nand, 0, 1, 0, 1, 0);
    nand_setio(s->nand, (addr >> 1) & 0xff);
    nand_setio(s->nand, (addr >> 9) & 0x3);
    nand_setio(s->nand, (addr >> 11) & 0xff);
    nand_setio(s->nand, (addr >> 19) & 0xff);
    nand_setio(s->nand, (addr >> 27) & 0x1);
	/* send command: read page (cycle2) */
    nand_setpins(s->nand, 1, 0, 0, 1, 0);
    nand_setio(s->nand, 0x30);
    /* read page data */
    nand_setpins(s->nand, 0, 0, 0, 1, 0);
    for (i = 0; i < 0x800 / 2; i++)
        *(p++) = nand_getio(s->nand);
    return 1;
}

/*read the xloader from NAND Flash into internal RAM*/
static int beagle_boot_from_nand(struct beagle_s *s)
{
	uint32_t	loadaddr, len;
	uint8_t nand_page[0x800],*load_dest;
	uint32_t nand_pages,i;

	/* The first two words(8 bytes) in first nand flash page have special meaning.
		First word:x-loader len
		Second word: x-load address in internal ram */
	beagle_nand_read_page(s,nand_page,0);
	len = *((uint32_t*)nand_page);
	loadaddr =  *((uint32_t*)(nand_page+4));
    fprintf(stderr, "%s: len = 0x%08x, addr = 0x%08x\n", __FUNCTION__, len, loadaddr);
    if ((len==0)||(loadaddr==0)||(len==0xffffffff)||(loadaddr==0xffffffff))
		return (-1);

	/*put the first page into internal ram*/
	load_dest = phys_ram_base + beagle_binfo.ram_size;
	load_dest += loadaddr-OMAP3_SRAM_BASE;
	
	memcpy(load_dest,nand_page+8,0x800-8);
	load_dest += 0x800-8;

	nand_pages = len/0x800;
	if (len%0x800!=0)
		nand_pages++;

	for (i=1;i<nand_pages;i++)
	{
		beagle_nand_read_page(s,nand_page,i*0x800);
		memcpy(load_dest,nand_page,0x800);
		load_dest += 0x800;
	}
	s->cpu->env->regs[15] = loadaddr;
	return 0;

}

static int beagle_rom_emu(struct beagle_s *s)
{
    if (!omap3_mmc_boot(s->cpu))
        if (beagle_boot_from_nand(s) < 0)
            return -1;
	return 0;
}

static void beagle_dss_setup(struct beagle_s *s)
{
	s->lcd_panel = omap3_lcd_panel_init();
	omap3_lcd_panel_attach(s->cpu->dss, 0, s->lcd_panel);
}

//static void beagle_mmc_cs_cb(void *opaque, int line, int level)
//{
//    /* TODO: this seems to actually be connected to the menelaus, to
//     * which also both MMC slots connect.  */
//    omap_mmc_enable((struct omap_mmc_s *) opaque, !level);
//
//    printf("%s: MMC slot %i active\n", __FUNCTION__, level + 1);
//}

static void beagle_i2c_setup(struct beagle_s *s)
{
    /* Attach the CPU on one end of our I2C bus.  */
    s->i2c = omap_i2c_bus(s->cpu->i2c[0]);

    s->twl4030 = twl4030_init(s->i2c, s->cpu->irq[0][OMAP_INT_35XX_SYS_NIRQ]);
}


static void beagle_init(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    struct beagle_s *s = (struct beagle_s *) qemu_mallocz(sizeof(*s));
    int sdram_size = beagle_binfo.ram_size;

   	if (ram_size < sdram_size +  OMAP3530_SRAM_SIZE) {
        fprintf(stderr, "This architecture uses %i bytes of memory\n",
                        sdram_size + OMAP3530_SRAM_SIZE);
        exit(1);
    }
   	s->cpu = omap3530_mpu_init(sdram_size, NULL);
   	beagle_nand_setup(s);
   	beagle_i2c_setup(s);
   	beagle_dss_setup(s);
   	omap3_set_device_type(s->cpu,GP_DEVICE);
    if (beagle_rom_emu(s) < 0) {
   		fprintf(stderr,"boot from MMC and nand failed \n");
   		exit(-1);
   	}
}



QEMUMachine beagle_machine = {
    .name = "beagle",
    .desc =     "Beagle board (OMAP3530)",
    .init =     beagle_init,
    .ram_require =     (0x08000000 +  OMAP3530_SRAM_SIZE) | RAMSIZE_FIXED,
};

