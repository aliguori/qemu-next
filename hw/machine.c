#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/boards.h"
#include "hw/sysbus.h"

#define MACHINE_STATE(dev) ((MachineState *)(dev))

DeviceState *machine_dev;

typedef struct MachineState
{
    SysBusDevice parent;
    void *machine;
    uint64_t ram_size;
    char *boot_device;
    char *kernel_filename;
    char *kernel_cmdline;
    char *initrd_filename;
    char *cpu_model;
} MachineState;

static int machine_initfn(SysBusDevice *dev)
{
    MachineState *m = MACHINE_STATE(dev);
    QEMUMachine *machine = m->machine;

    machine->init(m->ram_size, m->boot_device, m->kernel_filename,
                  m->kernel_cmdline, m->initrd_filename, m->cpu_model);

    return 0;
}

static SysBusDeviceInfo machine_devinfo = {
    .qdev.name = "machine",
    .qdev.no_user = 1,
    .qdev.size = sizeof(MachineState),
    .init = machine_initfn,
    .qdev.props = (Property[]){
        DEFINE_PROP_UINT64("ram_size", MachineState, ram_size, 0),
        DEFINE_PROP_STRING("boot_device", MachineState, boot_device),
        DEFINE_PROP_STRING("kernel_filename", MachineState, kernel_filename),
        DEFINE_PROP_STRING("kernel_cmdline", MachineState, kernel_cmdline),
        DEFINE_PROP_STRING("initrd_filename", MachineState, initrd_filename),
        DEFINE_PROP_STRING("cpu_model", MachineState, cpu_model),
        DEFINE_PROP_PTR("machine", MachineState, machine),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void register_devices(void)
{
    sysbus_register_withprop(&machine_devinfo);
}

device_init(register_devices);
