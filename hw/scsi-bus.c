#include "hw.h"
#include "sysemu.h"
#include "scsi-disk.h"
#include "qdev.h"

static struct BusInfo scsi_bus_info = {
    .name  = "SCSI",
    .size  = sizeof(SCSIBus),
};
static int next_scsi_bus;

/* Create a scsi bus, and attach devices to it.  */
SCSIBus *scsi_bus_new(DeviceState *host, int tcq,
                      SCSIAttachFn attach, scsi_completionfn complete)
{
    SCSIBus *bus;
    char name[32];

    snprintf(name, sizeof(name), "scsi%d", next_scsi_bus);
    bus = FROM_QBUS(SCSIBus, qbus_create(&scsi_bus_info, host, name));
    bus->busnr = next_scsi_bus++;
    bus->tcq = tcq;
    bus->attach = attach;
    bus->complete = complete;
    return bus;
}

void scsi_bus_attach_cmdline(SCSIBus *bus)
{
    int unit;
    int index;

    for (unit = 0; unit < MAX_SCSI_DEVS; unit++) {
        index = drive_get_index(IF_SCSI, bus->busnr, unit);
        if (index == -1) {
            continue;
        }
        bus->attach(bus->qbus.parent, drives_table[index].bdrv, unit);
    }
}

static void scsi_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    SCSIDevice *dev = DO_UPCAST(SCSIDevice, qdev, qdev);
    SCSIDeviceInfo *info = DO_UPCAST(SCSIDeviceInfo, qdev, base);

    dev->info = info;
    dev->info->init(dev);
}

void scsi_qdev_register(SCSIDeviceInfo *info)
{
    info->qdev.bus_info = &scsi_bus_info;
    info->qdev.init     = scsi_qdev_init;
    qdev_register(&info->qdev);
}

SCSIDevice *scsi_create_simple(SCSIBus *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_init(dev);
    return DO_UPCAST(SCSIDevice, qdev, dev);
}
