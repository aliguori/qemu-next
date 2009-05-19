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
#include "sysemu.h"
#include "hw.h"
#include "pc.h"
#include "hw/isa.h"
#include "hw/boards.h"

#define APIC_BASE 0x1000
#define APIC_SIZE 0x100

#define APIC_REG_NCPU        0x00
#define APIC_REG_ID          0x04
#define APIC_REG_SIPI_ADDR   0x08
#define APIC_REG_SEND_SIPI   0x0c
#define APIC_REG_IPI_VECTOR  0x10
#define APIC_REG_SEND_IPI    0x14

extern int (*cpu_get_pic_interrupt_override)(CPUState *env);
static int pending_interrupt;
static int apic_ipi_vector = 0xFF;

static uint32_t apic_sipi_addr;

static void apic_send_sipi(int vcpu)
{
}

static void apic_send_ipi(int vcpu)
{
        pending_interrupt = value;
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HARD);
}

static uint32_t apic_io_read(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = -1u;

    switch (addr - APIC_BASE) {
    case APIC_REG_NCPU:
        value = smp_cpus;
        break;
    case APIC_REG_ID:
        value = cpu_single_env->cpu_index;
        break;
    case APIC_REG_SIPI_ADDR:
        value = apic_sipi_addr;
        break;
    case APIC_REG_IPI_VECTOR:
        value = apic_ipi_vector;
        break;
    }

    return value;
}

static void apic_io_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    switch (addr - APIC_BASE) {
    case APIC_REG_SIPI_ADDR:
        apic_sipi_addr = value;
        break;
    case APIC_REG_SEND_SIPI:
        apic_send_sipi(value);
        break;
    case APIC_REG_IPI_VECTOR:
        apic_ipi_vector = value;
        break;
    case APIC_REG_SEND_IPI:
        apic_send_ipi(value);
        break;
    }
}

static void do_apic_init(void)
{
    register_ioport_read(APIC_BASE, APIC_SIZE, 4, apic_io_read, NULL);
    register_ioport_write(APIC_BASE, APIC_SIZE, 4, apic_io_write, NULL);
}

static void misc_io(void *opaque, uint32_t addr, uint32_t value)
{
    static int newline = 1;

    switch (addr) {
    case 0xff: // irq injector
        printf("injecting interrupt 0x%x\n", value);
        pending_interrupt = value;
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HARD);
        break;
    case 0xf1: // serial
        if (newline)
            fputs("GUEST: ", stdout);
        putchar(value);
        newline = value == '\n';
        break;
    case 0xd1:
        value = ram_size;
        break;
    case 0xf4: // exit
        exit(value);
        break;
    }
}

static void misc_init(void)
{
    register_ioport_write(0xff, 1, 1, misc_io, NULL);
    register_ioport_write(0xf1, 1, 1, misc_io, NULL);
    register_ioport_write(0xf4, 1, 1, misc_io, NULL);
    register_ioport_write(0xd1, 1, 1, misc_io, NULL);
}

static int get_pic_interrupt(CPUState *env)
{
    return pending_interrupt;
}

static void pc_test_init(ram_addr_t ram_size,
                         const char *boot_device,
                         const char *kernel_filename,
                         const char *kernel_cmdline,
                         const char *initrd_filename,
                         const char *cpu_model)
{
    ram_addr_t ram_addr;
    ram_addr_t below_4g_mem_size, above_4g_mem_size = 0;
    int max_sz, i;
    CPUState *env;

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        below_4g_mem_size = ram_size;
    }

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
    }
    
    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find x86 CPU definition\n");
            exit(1);
        }
        if (i != 0)
            env->halted = 1;
    }

    /* allocate RAM */
    ram_addr = qemu_ram_alloc(0xa0000);
    cpu_register_physical_memory(0, 0xa0000, ram_addr);

    /* Allocate, even though we won't register, so we don't break the
     * phys_ram_base + PA assumption. This range includes vga (0xa0000 - 0xc0000),
     * and some bios areas, which will be registered later
     */
    ram_addr = qemu_ram_alloc(0x100000 - 0xa0000);
    ram_addr = qemu_ram_alloc(below_4g_mem_size - 0x100000);
    cpu_register_physical_memory(0x100000,
                 below_4g_mem_size - 0x100000,
                 ram_addr);

    /* above 4giga memory allocation */
    if (above_4g_mem_size > 0) {
        ram_addr = qemu_ram_alloc(above_4g_mem_size);
        cpu_register_physical_memory(0x100000000ULL,
                                     above_4g_mem_size,
                                     ram_addr);
    }

    max_sz = get_image_size(kernel_filename);
    load_image_targphys(kernel_filename, 0xf0000, max_sz);

    max_sz = get_image_size(initrd_filename);
    load_image_targphys(initrd_filename, 0x100000, max_sz);

    cpu_get_pic_interrupt_override = get_pic_interrupt;

    do_apic_init();
    misc_init();
}

static QEMUMachine pc_test_machine = {
    .name = "pc-test",
    .desc = "Test harness for PC",
    .init = pc_test_init,
    .max_cpus = 255,
};

static void pc_test_machine_init(void)
{
    qemu_register_machine(&pc_test_machine);
}

machine_init(pc_test_machine_init);
