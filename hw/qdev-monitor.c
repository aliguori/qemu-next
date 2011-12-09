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

/* The theory here is that it should be possible to create a machine without
   knowledge of specific devices.  Historically board init routines have
   passed a bunch of arguments to each device, requiring the board know
   exactly which device it is dealing with.  This file provides an abstract
   API for device configuration and initialization.  Devices will generally
   inherit from a particular bus (e.g. PCI or I2C) rather than
   this API directly.  */

#include "qdev.h"
#include "monitor.h"

#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_prop(Monitor *mon, DeviceState *dev, Property *prop,
                            const char *prefix, int indent)
{
    char buf[64];

    /*
     * TODO Properties without a print method are just for dirty
     * hacks.  qdev_prop_ptr is the only such PropertyInfo.  It's
     * marked for removal.  The test props->info->print should be
     * removed along with it.
     */
    if (prop->info->print) {
        prop->info->print(dev, prop, buf, sizeof(buf));
        qdev_printf("%s-prop: %s = %s\n", prefix, prop->name, buf);
    }
}

static void qdev_print(Monitor *mon, DeviceState *dev, int indent)
{
    DeviceProperty *dev_prop;
    BusState *child;
    qdev_printf("dev: %s, id \"%s\"\n", object_get_type(OBJECT(dev)),
                dev->id ? dev->id : "");
    indent += 2;
    if (dev->num_gpio_in) {
        qdev_printf("gpio-in %d\n", dev->num_gpio_in);
    }
    if (dev->num_gpio_out) {
        qdev_printf("gpio-out %d\n", dev->num_gpio_out);
    }
    QTAILQ_FOREACH(dev_prop, &dev->properties, node) {
        if (!strstart(dev_prop->type, "legacy<", NULL)) {
            continue;
        }
        qdev_print_prop(mon, dev, dev_prop->opaque, "dev", indent);
    }
    if (dev->parent_bus && dev->parent_bus->info->print_dev) {
        dev->parent_bus->info->print_dev(mon, dev, indent);
    }

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
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        qdev_print(mon, dev, indent);
    }
}
#undef qdev_printf

void do_info_qtree(Monitor *mon)
{
    qbus_print(mon, sysbus_get_default(), 0);
}

static void info_qdm_one(ObjectClass *klass, void *data)
{
    DeviceClass *info;

    if (!object_class_dynamic_cast(klass, TYPE_DEVICE)) {
        return;
    }

    info = DEVICE_CLASS(klass);

    error_printf("name \"%s\", bus %s",
                 object_class_get_name(klass),
                 info->bus_info->name);
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
    object_class_foreach(info_qdm_one, mon);
}

int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    QemuOpts *opts;

    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict);
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

    dev = qdev_find_recursive(sysbus_get_default(), id);
    if (NULL == dev) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    return qdev_unplug(dev);
}
