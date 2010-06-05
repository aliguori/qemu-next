/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "qdev.h"

typedef void QEMUMachineInitFunc(QemuOpts *opts);

typedef struct QEMUMachine {
    QEMUMachineInitFunc *init;
    QemuOptDesc *opts_desc;
    QemuOptValue *opts_default;
    QTAILQ_ENTRY(QEMUMachine) node;
} QEMUMachine;

int qemu_register_machine(QEMUMachine *machine);
void machine_set_default(const char *name);

#define QOPT_COMPAT(driver, property, value) \
    QOPT_VALUE(driver "." property, value)

#define QOPT_COMPAT_INT(driver, property, value) \
    QOPT_VALUE(driver "." property, stringify(value))

#define COMMON_MACHINE_OPTS() 		\
    {                                   \
        .name = "name",                 \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "driver",               \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "desc",                 \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "ram_size",             \
        .type = QEMU_OPT_SIZE,          \
    },{                                 \
        .name = "kernel",               \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "cmdline",              \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "initrd",               \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "boot_device",          \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "cpu",                  \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "serial",               \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "parallel",             \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "virtcon",              \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "vga",                  \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "floppy",               \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "cdrom",                \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "sdcard",               \
        .type = QEMU_OPT_BOOL,          \
    },{                                 \
        .name = "default_drive",        \
        .type = QEMU_OPT_STRING,        \
    },{                                 \
        .name = "max_cpus",             \
        .type = QEMU_OPT_NUMBER,        \
    },{                                 \
        .name = "sockets",              \
        .type = QEMU_OPT_NUMBER,        \
    },{                                 \
        .name = "accel",                \
        .type = QEMU_OPT_STRING,        \
    }


#endif
