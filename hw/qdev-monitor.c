/*
 *  Dynamic device configuration and creation.
 *
 *  Copyright (c) 2009 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qdev-monitor.h"
#include "qdev-devadd.h"
#include "monitor.h"
#include "sysemu.h"

#include "isa.h"
#include "pci.h"
#include "sysbus.h"
#include "usb.h"
#include "virtio-serial.h"

#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_props(Monitor *mon, DeviceState *dev, Property *props,
                             const char *prefix, int indent)
{
    char buf[64];

    if (!props)
        return;
    while (props->name) {
        /*
         * TODO Properties without a print method are just for dirty
         * hacks.  qdev_prop_ptr is the only such PropertyInfo.  It's
         * marked for removal.  The test props->info->print should be
         * removed along with it.
         */
        if (props->info->print) {
            props->info->print(dev, props, buf, sizeof(buf));
            qdev_printf("%s-prop: %s = %s\n", prefix, props->name, buf);
        }
        props++;
    }
}

static void isabus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    ISADevice *d = DO_UPCAST(ISADevice, qdev, dev);

    if (d->isairq[1] != -1) {
        monitor_printf(mon, "%*sisa irqs %d,%d\n", indent, "",
                       d->isairq[0], d->isairq[1]);
    } else if (d->isairq[0] != -1) {
        monitor_printf(mon, "%*sisa irq %d\n", indent, "",
                       d->isairq[0]);
    }
}

static void pcibus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    PCIDevice *d = (PCIDevice *)dev;
    const char *desc;
    char ctxt[64];
    PCIIORegion *r;
    int i, class;

    class = pci_get_word(d->config + PCI_CLASS_DEVICE);
    desc = pci_class_get_description(class);
    if (desc) {
        snprintf(ctxt, sizeof(ctxt), "%s", desc);
    } else {
        snprintf(ctxt, sizeof(ctxt), "Class %04x", class);
    }

    monitor_printf(mon, "%*sclass %s, addr %02x:%02x.%x, "
                   "pci id %04x:%04x (sub %04x:%04x)\n",
                   indent, "", ctxt,
                   d->config[PCI_SECONDARY_BUS],
                   PCI_SLOT(d->devfn), PCI_FUNC(d->devfn),
                   pci_get_word(d->config + PCI_VENDOR_ID),
                   pci_get_word(d->config + PCI_DEVICE_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_VENDOR_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_ID));
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (!r->size)
            continue;
        monitor_printf(mon, "%*sbar %d: %s at 0x%"FMT_PCIBUS
                       " [0x%"FMT_PCIBUS"]\n",
                       indent, "",
                       i, r->type & PCI_BASE_ADDRESS_SPACE_IO ? "i/o" : "mem",
                       r->addr, r->addr + r->size - 1);
    }
}

static void sysbus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    SysBusDevice *s = sysbus_from_qdev(dev);
    int i;

    for (i = 0; i < s->num_mmio; i++) {
        monitor_printf(mon, "%*smmio " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                       indent, "", s->mmio[i].addr, s->mmio[i].size);
    }
}

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBBus *bus = usb_bus_from_device(dev);

    monitor_printf(mon, "%*saddr %d.%d, speed %s, name %s%s\n",
                   indent, "", bus->busnr, dev->addr,
                   usb_speed(dev->speed), dev->product_desc,
                   dev->attached ? ", attached" : "");
}

static void virtser_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent)
{
    VirtIOSerialDevice *dev = DO_UPCAST(VirtIOSerialDevice, qdev, qdev);
    VirtIOSerialPort *port = DO_UPCAST(VirtIOSerialPort, dev, &dev->qdev);

    monitor_printf(mon, "%*s dev-prop-int: id: %u\n",
                   indent, "", port->id);
    monitor_printf(mon, "%*s dev-prop-int: guest_connected: %d\n",
                   indent, "", port->guest_connected);
    monitor_printf(mon, "%*s dev-prop-int: host_connected: %d\n",
                   indent, "", port->host_connected);
    monitor_printf(mon, "%*s dev-prop-int: throttled: %d\n",
                   indent, "", port->throttled);
}

static void qdev_print_bus_info(Monitor *mon, DeviceState *dev, int indent)
{
    const char *bus = dev->parent_bus->info->name;

    if (!strcmp(bus, "ISA")) {
        isabus_dev_print(mon, dev, indent);
    } else if (!strcmp(bus, "PCI")) {
        pcibus_dev_print(mon, dev, indent);
    } else if (!strcmp(bus, "System")) {
        sysbus_dev_print(mon, dev, indent);
    } else if (!strcmp(bus, "USB")) {
        usb_bus_dev_print(mon, dev, indent);
    } else if (!strcmp(bus, "virtio-serial-bus")) {
        virtser_bus_dev_print(mon, dev, indent);
    }
}

static void qdev_print(Monitor *mon, DeviceState *dev, int indent)
{
    BusState *child;
    qdev_printf("dev: %s, id \"%s\"\n", dev->info->name,
                dev->id ? dev->id : "");
    indent += 2;
    if (dev->num_gpio_in) {
        qdev_printf("gpio-in %d\n", dev->num_gpio_in);
    }
    if (dev->num_gpio_out) {
        qdev_printf("gpio-out %d\n", dev->num_gpio_out);
    }
    qdev_print_props(mon, dev, dev->info->props, "dev", indent);
    qdev_print_props(mon, dev, dev->parent_bus->info->props, "bus", indent);

    qdev_print_bus_info(mon, dev, indent);

    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        qbus_print(mon, child, indent);
    }
}

static void qbus_print(Monitor *mon, BusState *bus, int indent)
{
    struct DeviceState *dev;

    qdev_printf("bus: %s\n", bus->name);
    indent += 2;
    qdev_printf("type %s\n", bus->info->name);
    QLIST_FOREACH(dev, &bus->children, sibling) {
        qdev_print(mon, dev, indent);
    }
}
#undef qdev_printf

void do_info_qtree(Monitor *mon)
{
    BusState *bus = sysbus_get_default();

    if (bus) {
        qbus_print(mon, bus, 0);
    }
}

static void qdev_print_devinfo(DeviceInfo *info)
{
    error_printf("name \"%s\", bus %s",
                 info->name, info->bus_info->name);
    if (info->alias) {
        error_printf(", alias \"%s\"", info->alias);
    }
    if (info->desc) {
        error_printf(", desc \"%s\"", info->desc);
    }
    if (info->no_user) {
        error_printf(", no-user");
    }
    error_printf("\n");
}

void do_info_qdm(Monitor *mon)
{
    DeviceInfo *info;

    for (info = device_info_list; info != NULL; info = info->next) {
        qdev_print_devinfo(info);
    }
}

int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    QemuOpts *opts;

    opts = qemu_opts_from_qdict(&qemu_device_opts, qdict);
    if (!opts) {
        return -1;
    }
    if (!monitor_cur_is_qmp() && qdev_device_help(opts)) {
        qemu_opts_del(opts);
        return 0;
    }
    if (!qdev_device_add(opts)) {
        qemu_opts_del(opts);
        return -1;
    }
    return 0;
}

int do_device_del(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *id = qdict_get_str(qdict, "id");
    DeviceState *dev;

    dev = qbus_find_child_dev(sysbus_get_default(), id);
    if (NULL == dev) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    return qdev_unplug(dev);
}
