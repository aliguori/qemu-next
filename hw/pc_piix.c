/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw.h"
#include "pc.h"
#include "fdc.h"
#include "pci.h"
#include "block.h"
#include "sysemu.h"
#include "audio/audio.h"
#include "net.h"
#include "smbus.h"
#include "boards.h"
#include "monitor.h"
#include "fw_cfg.h"
#include "hpet_emul.h"
#include "watchdog.h"
#include "smbios.h"

#define MAX_IDE_BUS 2

static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };

enum {
    COMPAT_DEFAULT = 0,
    COMPAT_0_10, /* compatible with qemu 0.10.x */
};

static void piix_pci_device_init(PCIBus *pci_bus, int compat_level)
{
    const char *virtio_blk_name;
    const char *virtio_console_name;

    switch (compat_level) {
    case COMPAT_DEFAULT:
    default:
        virtio_blk_name = "virtio-blk-pci";
        virtio_console_name = "virtio-console-pci";
        break;

    case COMPAT_0_10:
        virtio_blk_name = "virtio-blk-pci-0-10";
        virtio_console_name = "virtio-console-pci-0-10";
        break;
    }

    pc_pci_device_init1(pci_bus, virtio_blk_name, virtio_console_name);
}

/* PC hardware initialisation */
static void pc_init1(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename,
                     const char *kernel_cmdline,
                     const char *initrd_filename,
                     const char *cpu_model,
                     int pci_enabled,
                     int compat_level)
{
    int i;
    ram_addr_t below_4g_mem_size, above_4g_mem_size;
    PCIBus *pci_bus;
    PCIDevice *i440fx_state;
    int piix3_devfn = -1;
    qemu_irq *cpu_irq;
    qemu_irq *i8259;
    int index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    fdctrl_t *floppy_controller;
    RTCState *rtc_state;

    pc_cpus_init(cpu_model);

    vmport_init();

    /* allocate ram and load rom/bios */
    pc_memory_init(ram_size, kernel_filename, kernel_cmdline, initrd_filename,
                   &below_4g_mem_size, &above_4g_mem_size);

    cpu_irq = pc_allocate_cpu_irq();
    i8259 = i8259_init(cpu_irq[0]);
    pc_register_ferr_irq(i8259[13]);

    if (pci_enabled) {
        pci_bus = i440fx_init(&i440fx_state, i8259);
        piix3_devfn = piix3_init(pci_bus, -1);
    } else {
        pci_bus = NULL;
    }

    pc_vga_init(pci_enabled? pci_bus: NULL);

    /* init basic PC hardware */
    pc_basic_device_init(i8259, &floppy_controller, &rtc_state);

    if (pci_enabled) {
        IOAPICState *ioapic = ioapic_init();
        pic_set_alt_irq_func(isa_pic, ioapic_set_irq, ioapic);
    }

    watchdog_pc_init(pci_bus);

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_enabled || (nd->model && strcmp(nd->model, "ne2k_isa") == 0))
            pc_init_ne2k_isa(nd, i8259);
        else
            pci_nic_init(nd, "ne2k_pci", NULL);
    }

    piix4_acpi_system_hot_add_init();

    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }

    for(i = 0; i < MAX_IDE_BUS * MAX_IDE_DEVS; i++) {
        index = drive_get_index(IF_IDE, i / MAX_IDE_DEVS, i % MAX_IDE_DEVS);
	if (index != -1)
	    hd[i] = drives_table[index].bdrv;
	else
	    hd[i] = NULL;
    }

    if (pci_enabled) {
        pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1, i8259);
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            isa_ide_init(ide_iobase[i], ide_iobase2[i], i8259[ide_irq[i]],
	                 hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
        }
    }

#ifdef HAS_AUDIO
    pc_audio_init(pci_enabled ? pci_bus : NULL, i8259);
#endif

    pc_cmos_init(below_4g_mem_size, above_4g_mem_size, boot_device, hd,
                 floppy_controller, rtc_state);

    if (pci_enabled && usb_enabled) {
        usb_uhci_piix3_init(pci_bus, piix3_devfn + 2);
    }

    if (pci_enabled && acpi_enabled) {
        uint8_t *eeprom_buf = qemu_mallocz(8 * 256); /* XXX: make this persistent */
        i2c_bus *smbus;

        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100, i8259[9]);
        for (i = 0; i < 8; i++) {
            DeviceState *eeprom;
            eeprom = qdev_create((BusState *)smbus, "smbus-eeprom");
            qdev_prop_set_uint32(eeprom, "address", 0x50 + i);
            qdev_prop_set_ptr(eeprom, "data", eeprom_buf + (i * 256));
            qdev_init(eeprom);
        }
    }

    if (i440fx_state) {
        i440fx_init_memory_mappings(i440fx_state);
    }

    if (pci_enabled) {
        piix_pci_device_init(pci_bus, compat_level);
    }
}

static void pc_init_pci(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    pc_init1(ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model,
             1, COMPAT_DEFAULT);
}

static void pc_init_isa(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    pc_init1(ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model,
             0, COMPAT_DEFAULT);
}

static void pc_init_pci_0_10(ram_addr_t ram_size,
                             const char *boot_device,
                             const char *kernel_filename,
                             const char *kernel_cmdline,
                             const char *initrd_filename,
                             const char *cpu_model)
{
    pc_init1(ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model,
             1, COMPAT_0_10);
}

static QEMUMachine pc_machine = {
    .name = "pc",
    .desc = "Standard PC",
    .init = pc_init_pci,
    .max_cpus = 255,
    .is_default = 1,
};

static QEMUMachine pc_machine_v0_10 = {
    .name = "pc-0.10",
    .desc = "Standard PC, qemu 0.10",
    .init = pc_init_pci,
    .max_cpus = 255,
    .compat_props = (CompatProperty[]) {
        {
            .driver   = "virtio-blk-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_STORAGE_OTHER),
        },{
            .driver   = "virtio-console-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_DISPLAY_OTHER),
        },{
            .driver   = "virtio-net-pci",
            .property = "vectors",
            .value    = stringify(0),
        },
        { /* end of list */ }
    },
};

static QEMUMachine isapc_machine = {
    .name = "isapc",
    .desc = "ISA-only PC",
    .init = pc_init_isa,
    .max_cpus = 1,
};

static QEMUMachine pc_0_10_machine = {
    .name = "pc-0-10",
    .desc = "Standard PC compatible with qemu 0.10.x",
    .init = pc_init_pci_0_10,
    .max_cpus = 255,
};

static void pc_machine_init(void)
{
    qemu_register_machine(&pc_machine);
    qemu_register_machine(&pc_machine_v0_10);
    qemu_register_machine(&isapc_machine);

    /* For compatibility with 0.10.x */
    qemu_register_machine(&pc_0_10_machine);
}

machine_init(pc_machine_init);
