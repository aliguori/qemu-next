/*
 * Dummy Machine Support
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "hw.h"
#include "sysemu.h"
#include "boards.h"

static void dummy_init(ram_addr_t ram_size,
                       const char *boot_device,
                       const char *kernel_filename,
                       const char *kernel_cmdline,
                       const char *initrd_filename,
                       const char *cpu_model)
{
}

static QEMUMachine dummy_machine = {
    .name = "none",
    .desc = "No Machine",
    .init = dummy_init,
};

static void dummy_machine_init(void)
{
    qemu_register_machine(&dummy_machine);
}

machine_init(dummy_machine_init);
