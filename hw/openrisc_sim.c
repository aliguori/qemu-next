/*
 * Openrisc simulator for use as an ISS.
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                          Feng Gao <gf91597@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include "hw.h"
#include "fdc.h"
#include "net.h"
#include "openrisc_cpudev.h"
#include "boards.h"
#include "pci.h"
#include "elf.h"
#include "smbus.h"
#include "memory.h"
#include "pc.h"
#include "pci.h"
#include "sysbus.h"
#include "flash.h"
#include "loader.h"
#include "exec-memory.h"
#include "sysemu.h"
#include "isa.h"
#include "mc146818rtc.h"
#include "blockdev.h"
#include "qemu-log.h"

#define KERNEL_LOAD_ADDR 0x100

static uint64_t translate_phys_addr(void *env, uint64_t addr)
{
    return cpu_get_phys_page_debug(env, addr);
}

static void main_cpu_reset(void *opaque)
{
    CPUOPENRISCState *env = opaque;
    openrisc_reset(env);
}

static void openrisc_sim_init(ram_addr_t ram_size,
                              const char *boot_device,
                              const char *kernel_filename,
                              const char *kernel_cmdline,
                              const char *initrd_filename,
                              const char *cpu_model)
{
    CPUOPENRISCState *env;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    int kernel_size;
    uint64_t elf_entry;
    target_phys_addr_t entry;
    qemu_irq *i8259;
    ISABus *isa_bus;

    if (!cpu_model) {
        cpu_model = "or1200";
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition!\n");
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, env);
    main_cpu_reset(env);

    memory_region_init_ram(ram, "openrisc.ram", ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, translate_phys_addr, env,
                               &elf_entry, NULL, NULL, 1, ELF_MACHINE, 1);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
            entry = KERNEL_LOAD_ADDR;
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        if (kernel_size > 0) {
            env->pc = elf_entry;
        }
    } else {
        entry = 0;
    }

    cpu_openrisc_pic_init(env);
    cpu_openrisc_clock_init(env);

    isa_bus = isa_bus_new(NULL, get_system_io());
    i8259 = i8259_init(isa_bus, env->irq[3]);
    isa_bus_irqs(isa_bus, i8259);

    serial_mm_init(get_system_memory(), 0x90000000, 0,
                   env->irq[2], 115200, serial_hds[0], DEVICE_NATIVE_ENDIAN);

    if (nd_table[0].vlan) {
        isa_ne2000_init(isa_bus, 0x92000000, 4, &nd_table[0]);
    }
}

static QEMUMachine openrisc_sim_machine = {
    .name = "or32-sim",
    .desc = "or32 simulation",
    .init = openrisc_sim_init,
    .max_cpus = 1,
    .is_default = 1,
};

static void openrisc_sim_machine_init(void)
{
    qemu_register_machine(&openrisc_sim_machine);
}

machine_init(openrisc_sim_machine_init);
