/*
 * Beagle board emulation. http://beagleboard.org/
 * 
 * Original code Copyright (C) 2008 yajin(yajin@vm-kernel.org)
 * Rewrite Copyright (C) 2009 Nokia Corporation
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
#include "boards.h"
#include "i2c.h"
#include "devices.h"
#include "flash.h"

#define BEAGLE_NAND_CS       0
#define BEAGLE_NAND_PAGESIZE 0x800
#define BEAGLE_SDRAM_SIZE    (128 * 1024 * 1024) /* 128MB */

/* Beagle board support */
struct beagle_s {
    struct omap_mpu_state_s *cpu;
    
    struct nand_flash_s *nand;
    struct omap3_lcd_panel_s *lcd_panel;
    i2c_bus *i2c;
    struct twl4030_s *twl4030;
};

static void beagle_init(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    struct beagle_s *s = (struct beagle_s *) qemu_mallocz(sizeof(*s));
    int sdindex = drive_get_index(IF_SD, 0, 0);
    
    if (sdindex == -1) {
        fprintf(stderr, "%s: missing SecureDigital device\n", __FUNCTION__);
        exit(1);
    }
   	s->cpu = omap3530_mpu_init(ram_size, NULL, NULL, serial_hds[0]);
    
	s->nand = nand_init(NAND_MFR_MICRON, 0xba); /* MT29F2G16ABC */
	nand_setpins(s->nand, 0, 0, 0, 1, 0); /* no write-protect */
    omap_gpmc_attach(s->cpu->gpmc, BEAGLE_NAND_CS, 0, NULL, NULL, s, s->nand);
    omap3_mmc_attach(s->cpu->omap3_mmc[0], drives_table[sdindex].bdrv);

    s->i2c = omap_i2c_bus(s->cpu->i2c[0]);
    s->twl4030 = twl4030_init(s->i2c, s->cpu->irq[0][OMAP_INT_3XXX_SYS_NIRQ]);

	s->lcd_panel = omap3_lcd_panel_init();
	omap3_lcd_panel_attach(s->cpu->dss, 0, s->lcd_panel);
    
    omap3_boot_rom_emu(s->cpu);
}

QEMUMachine beagle_machine = {
    .name =        "beagle",
    .desc =        "Beagle board (OMAP3530)",
    .init =        beagle_init,
    .ram_require = OMAP3XXX_SRAM_SIZE + OMAP3XXX_BOOTROM_SIZE,
};

