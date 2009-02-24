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
    
    struct nand_bflash_s *nand;
    struct omap3_lcd_panel_s *lcd_panel;
    i2c_bus *i2c;
    struct twl4030_s *twl4030;
};



static struct arm_boot_info beagle_binfo = {
    .ram_size = 0x08000000,
};


static uint32_t beagle_nand_read16(void *opaque, target_phys_addr_t addr)
{
	struct beagle_s *s = (struct beagle_s *) opaque;
    //BEAGLE_DEBUG("beagle_nand_read16 offset %x\n",addr);

	switch (addr)
	{
		case 0x7C: /*NAND_COMMAND*/
		case 0x80: /*NAND_ADDRESS*/
			OMAP_BAD_REG(addr);
			break;
		case 0x84: /*NAND_DATA*/
			return nandb_read_data16(s->nand);
			break;
		default:
			OMAP_BAD_REG(addr);
			break;
	}
    return 0;
}

static void beagle_nand_write16(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
	struct beagle_s *s = (struct beagle_s *) opaque;
    switch (addr)
	{
		case 0x7C: /*NAND_COMMAND*/
			nandb_write_command(s->nand,value);
			break;
		case 0x80: /*NAND_ADDRESS*/
			nandb_write_address(s->nand,value);
			break;
		case 0x84: /*NAND_DATA*/
			nandb_write_data16(s->nand,value);
			break;
		default:
			OMAP_BAD_REG(addr);
			break;
	}
}


static CPUReadMemoryFunc *beagle_nand_readfn[] = {
        beagle_nand_read16,
        beagle_nand_read16,
        omap_badwidth_read32,
};

static CPUWriteMemoryFunc *beagle_nand_writefn[] = {
        beagle_nand_write16,
        beagle_nand_write16,
        omap_badwidth_write32,
};

static void beagle_nand_setup(struct beagle_s *s)
{
	//int iomemtype;
	
	/*MT29F2G16ABC*/
	s->nand = nandb_init(NAND_MFR_MICRON,0xba);
	/*wp=1, no write protect!!! */
	//nand_set_wp(s->nand, 1);

/*	iomemtype = cpu_register_io_memory(0, beagle_nand_readfn,
                    beagle_nand_writefn, s);
    cpu_register_physical_memory(0x6e00007c, 0xc, iomemtype);*/
    omap_gpmc_attach(s->cpu->gpmc, 0, 0, NULL, NULL, s, beagle_nand_readfn, beagle_nand_writefn);

	 /*BOOT from nand*/
    omap3_set_mem_type(s->cpu,GPMC_NAND);

}

static int beagle_nand_read_page(struct beagle_s *s,uint8_t *buf, uint16_t page_addr)
{
	uint16_t *p;
	int i;

	p=(uint16_t *)buf;

	/*send command 0x0*/
	beagle_nand_write16(s,0x7C,0);
	/*send page address */
	beagle_nand_write16(s,0x80,page_addr&0xff);
	beagle_nand_write16(s,0x80,(page_addr>>8)&0x7);
	beagle_nand_write16(s,0x80,(page_addr>>11)&0xff);
	beagle_nand_write16(s,0x80,(page_addr>>19)&0xff);
	beagle_nand_write16(s,0x80,(page_addr>>27)&0xff);
	/*send command 0x30*/
	beagle_nand_write16(s,0x7C,0x30);

	for (i=0;i<0x800/2;i++)
	{
		*p++ = beagle_nand_read16(s,0x84);
	}
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
	if ((len==0)||(loadaddr==0)||(len==0xffffffff)||(loadaddr==0xffffffff))
		return (-1);

	/*put the first page into internal ram*/
	load_dest = phys_ram_base +beagle_binfo.ram_size;
	load_dest += loadaddr-OMAP3_SRAM_BASE;
	
	BEAGLE_DEBUG("load_dest %x phys_ram_base %x \n",(unsigned)load_dest,(unsigned)phys_ram_base);
	
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
	s->lcd_panel->dss = s->cpu->dss;
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

