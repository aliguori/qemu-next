#include "hw.h"
#include "usb.h"
#include "qdev.h"

static struct BusInfo usb_bus_info = {
    .name  = "USB",
    .size  = sizeof(USBBus),
};
static int next_usb_bus = 0;
static USBBus *usbbus; /* hack alert */

USBBus *usb_bus_new(DeviceState *host)
{
    USBBus *bus;
    char name[32];

    snprintf(name, sizeof(name), "usb%d", next_usb_bus);
    bus = FROM_QBUS(USBBus, qbus_create(&usb_bus_info, host, name));
    if (!usbbus)
        usbbus = bus;
    next_usb_bus++;
    return bus;
}

static void usb_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBDeviceInfo *info = DO_UPCAST(USBDeviceInfo, qdev, base);

    pstrcpy(dev->devname, sizeof(dev->devname), qdev->info->name);
    dev->info = info;
    dev->info->init(dev);
}

void usb_qdev_register(USBDeviceInfo *info, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        info[i].qdev.bus_info = &usb_bus_info;
        info[i].qdev.init     = usb_qdev_init;
        qdev_register(&info[i].qdev);
    }
}

USBDevice *usb_create_simple(USBBus *bus, const char *name)
{
    DeviceState *dev;

#if 1
    /* temporary stopgap until all usb is properly qdev-ified */
    if (!bus)
        bus = usbbus;
    if (!bus)
        bus = usb_bus_new(NULL);
#endif

    dev = qdev_create(&bus->qbus, name);
    qdev_init(dev);
    return DO_UPCAST(USBDevice, qdev, dev);
}
