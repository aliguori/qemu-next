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

#include "net.h"
#include "qdev.h"
#include "sysemu.h"

static int qdev_hotplug = 0;

/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
static BusState *main_system_bus;

DeviceInfo *device_info_list;

/* Register a new device type.  */
void qdev_register(DeviceInfo *info)
{
    assert(info->size >= sizeof(DeviceState));
    assert(!info->next);

    info->next = device_info_list;
    device_info_list = info;
}

DeviceInfo *qdev_find_info(BusInfo *bus_info, const char *name)
{
    DeviceInfo *info;

    /* first check device names */
    for (info = device_info_list; info != NULL; info = info->next) {
        if (bus_info && info->bus_info != bus_info)
            continue;
        if (strcmp(info->name, name) != 0)
            continue;
        return info;
    }

    /* failing that check the aliases */
    for (info = device_info_list; info != NULL; info = info->next) {
        if (bus_info && info->bus_info != bus_info)
            continue;
        if (!info->alias)
            continue;
        if (strcmp(info->alias, name) != 0)
            continue;
        return info;
    }
    return NULL;
}

static DeviceState *qdev_create_from_info(BusState *bus, DeviceInfo *info)
{
    DeviceState *dev;

    assert(bus->info == info->bus_info);
    dev = qemu_mallocz(info->size);
    dev->info = info;
    dev->parent_bus = bus;
    qdev_prop_set_defaults(dev, dev->info->props);
    qdev_prop_set_defaults(dev, dev->parent_bus->info->props);
    qdev_prop_set_globals(dev);
    QLIST_INSERT_HEAD(&bus->children, dev, sibling);
    if (qdev_hotplug) {
        assert(bus->allow_hotplug);
        dev->hotplugged = 1;
    }
    dev->instance_id_alias = -1;
    dev->state = DEV_STATE_CREATED;
    return dev;
}

/* Create a new device.  This only initializes the device state structure
   and allows properties to be set.  qdev_init should be called to
   initialize the actual device emulation.  */
DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceInfo *info;

    if (!bus) {
        if (!main_system_bus) {
            main_system_bus = qbus_create(&system_bus_info, NULL, "main-system-bus");
        }
        bus = main_system_bus;
    }

    info = qdev_find_info(bus->info, name);
    if (!info) {
        hw_error("Unknown device '%s' for bus '%s'\n", name, bus->info->name);
    }

    return qdev_create_from_info(bus, info);
}

/* Initialize a device.  Device properties should be set before calling
   this function.  IRQs and MMIO regions should be connected/mapped after
   calling this function.
   On failure, destroy the device and return negative value.
   Return 0 on success.  */
int qdev_init(DeviceState *dev)
{
    int rc;

    assert(dev->state == DEV_STATE_CREATED);
    rc = dev->info->init(dev, dev->info);
    if (rc < 0) {
        qdev_free(dev);
        return rc;
    }
    if (dev->info->vmsd) {
        vmstate_register_with_alias_id(dev, -1, dev->info->vmsd, dev,
                                       dev->instance_id_alias,
                                       dev->alias_required_for_version);
    }
    dev->state = DEV_STATE_INITIALIZED;
    if (dev->info->reset) {
        dev->info->reset(dev);
    }
    return 0;
}

void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version)
{
    assert(dev->state == DEV_STATE_CREATED);
    dev->instance_id_alias = alias_id;
    dev->alias_required_for_version = required_for_version;
}

int qdev_unplug(DeviceState *dev)
{
    if (!dev->parent_bus->allow_hotplug) {
#if 0
        /* FIXME */
        qerror_report(QERR_BUS_NO_HOTPLUG, dev->parent_bus->name);
#endif
        return -1;
    }
    assert(dev->info->unplug != NULL);

    return dev->info->unplug(dev);
}

static int qdev_reset_one(DeviceState *dev, void *opaque)
{
    if (dev->info->reset) {
        dev->info->reset(dev);
    }

    return 1;
}

BusState *sysbus_get_default(void)
{
    return main_system_bus;
}

void qbus_reset_all(BusState *bus)
{
    qbus_walk_child_devs(bus, qdev_reset_one, NULL);
}

/* can be used as ->unplug() callback for the simple cases */
int qdev_simple_unplug_cb(DeviceState *dev)
{
    /* just zap it */
    qdev_free(dev);
    return 0;
}

/* Like qdev_init(), but terminate program via hw_error() instead of
   returning an error value.  This is okay during machine creation.
   Don't use for hotplug, because there callers need to recover from
   failure.  Exception: if you know the device's init() callback can't
   fail, then qdev_init_nofail() can't fail either, and is therefore
   usable even then.  But relying on the device implementation that
   way is somewhat unclean, and best avoided.  */
void qdev_init_nofail(DeviceState *dev)
{
    DeviceInfo *info = dev->info;

    if (qdev_init(dev) < 0) {
        hw_error("Initialization of device %s failed\n", info->name);
    }
}

/* Unlink device from bus and free the structure.  */
void qdev_free(DeviceState *dev)
{
    BusState *bus;
    Property *prop;

    if (dev->state == DEV_STATE_INITIALIZED) {
        while (dev->num_child_bus) {
            bus = QLIST_FIRST(&dev->child_bus);
            qbus_free(bus);
        }
        if (dev->info->vmsd)
            vmstate_unregister(dev, dev->info->vmsd, dev);
        if (dev->info->exit)
            dev->info->exit(dev);
        if (dev->opts)
            qemu_opts_del(dev->opts);
    }
    QLIST_REMOVE(dev, sibling);
    for (prop = dev->info->props; prop && prop->name; prop++) {
        if (prop->info->free) {
            prop->info->free(dev, prop);
        }
    }
    qemu_free(dev);
}

void qdev_machine_creation_done(void)
{
    /*
     * ok, initial machine setup is done, starting from now we can
     * only create hotpluggable devices
     */
    qdev_hotplug = 1;
}

/* HACK: should go away */
int qdev_allow_hotplug(void)
{
    return qdev_hotplug;
}

/* Get a character (serial) device interface.  */
CharDriverState *qdev_init_chardev(DeviceState *dev)
{
    static int next_serial;

    /* FIXME: This function needs to go away: use chardev properties!  */
    return serial_hds[next_serial++];
}

BusState *qdev_get_parent_bus(DeviceState *dev)
{
    return dev->parent_bus;
}

void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n)
{
    assert(dev->num_gpio_in == 0);
    dev->num_gpio_in = n;
    dev->gpio_in = qemu_allocate_irqs(handler, dev, n);
}

void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n)
{
    assert(dev->num_gpio_out == 0);
    dev->num_gpio_out = n;
    dev->gpio_out = pins;
}

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n)
{
    assert(n >= 0 && n < dev->num_gpio_in);
    return dev->gpio_in[n];
}

void qdev_connect_gpio_out(DeviceState * dev, int n, qemu_irq pin)
{
    assert(n >= 0 && n < dev->num_gpio_out);
    dev->gpio_out[n] = pin;
}

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd)
{
    qdev_prop_set_macaddr(dev, "mac", nd->macaddr);
    if (nd->vlan)
        qdev_prop_set_vlan(dev, "vlan", nd->vlan);
    if (nd->netdev)
        qdev_prop_set_netdev(dev, "netdev", nd->netdev);
    if (nd->nvectors != DEV_NVECTORS_UNSPECIFIED &&
        qdev_prop_exists(dev, "vectors")) {
        qdev_prop_set_uint32(dev, "vectors", nd->nvectors);
    }
}

static int next_block_unit[IF_COUNT];

/* Get a block device.  This should only be used for single-drive devices
   (e.g. SD/Floppy/MTD).  Multi-disk devices (scsi/ide) should use the
   appropriate bus.  */
BlockDriverState *qdev_init_bdrv(DeviceState *dev, BlockInterfaceType type)
{
    int unit = next_block_unit[type]++;
    DriveInfo *dinfo;

    dinfo = drive_get(type, 0, unit);
    return dinfo ? dinfo->bdrv : NULL;
}

BusState *qdev_get_child_bus(DeviceState *dev, const char *name)
{
    BusState *bus;

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        if (strcmp(name, bus->name) == 0) {
            return bus;
        }
    }
    return NULL;
}

int qbus_walk_child_devs(BusState *bus, qdev_walkerfn *walker, void *opaque)
{
    DeviceState *dev;

    QLIST_FOREACH(dev, &bus->children, sibling) {
        BusState *child;

        if (!walker(dev, opaque)) {
            return 0;
        }

        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            if (!qbus_walk_child_devs(child, walker, opaque)) {
                return 0;
            }
        }
    }

    return 1;
}

int qbus_walk_child_busses(BusState *bus, qbus_walkerfn *walker, void *opaque)
{
    DeviceState *dev;

    QLIST_FOREACH(dev, &bus->children, sibling) {
        BusState *child;

        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            if (!walker(child, opaque)) {
                return 0;
            }

            if (!qbus_walk_child_busses(child, walker, opaque)) {
                return 0;
            }
        }
    }

    return 1;
}

typedef struct FindData
{
    /* input */
    const char *name;

    /* output */
    BusState *bus;
    DeviceState *dev;
} FindData;

static int qbus_match_bus(BusState *bus, void *opaque)
{
    FindData *data = opaque;

    if (strcmp(bus->name, data->name) != 0) {
        data->bus = bus;
        return 0;
    }

    return 1;
}

BusState *qbus_find_child_bus(BusState *bus, const char *id)
{
    FindData data = { .name = id };

    if (!qbus_walk_child_busses(bus, qbus_match_bus, &data)) {
        return data.bus;
    }

    return NULL;
}

static int qbus_match_dev(DeviceState *dev, void *opaque)
{
    FindData *data = opaque;

    if (dev->id && strcmp(dev->id, data->name) == 0) {
        data->dev = dev;
        return 0;
    }

    return 1;
}

DeviceState *qbus_find_child_dev(BusState *bus, const char *id)
{
    FindData data = { .name = id };

    if (!qbus_walk_child_devs(bus, qbus_match_dev, &data)) {
        return data.dev;
    }

    return NULL;
}

void qbus_create_inplace(BusState *bus, BusInfo *info,
                         DeviceState *parent, const char *name)
{
    char *buf;
    int i,len;

    bus->info = info;
    bus->parent = parent;

    if (name) {
        /* use supplied name */
        bus->name = qemu_strdup(name);
    } else if (parent && parent->id) {
        /* parent device has id -> use it for bus name */
        len = strlen(parent->id) + 16;
        buf = qemu_malloc(len);
        snprintf(buf, len, "%s.%d", parent->id, parent->num_child_bus);
        bus->name = buf;
    } else {
        /* no id -> use lowercase bus type for bus name */
        len = strlen(info->name) + 16;
        buf = qemu_malloc(len);
        len = snprintf(buf, len, "%s.%d", info->name,
                       parent ? parent->num_child_bus : 0);
        for (i = 0; i < len; i++)
            buf[i] = qemu_tolower(buf[i]);
        bus->name = buf;
    }

    QLIST_INIT(&bus->children);
    if (parent) {
        QLIST_INSERT_HEAD(&parent->child_bus, bus, sibling);
        parent->num_child_bus++;
    }

}

BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name)
{
    BusState *bus;

    bus = qemu_mallocz(info->size);
    bus->qdev_allocated = 1;
    qbus_create_inplace(bus, info, parent, name);
    return bus;
}

void qbus_free(BusState *bus)
{
    DeviceState *dev;

    while ((dev = QLIST_FIRST(&bus->children)) != NULL) {
        qdev_free(dev);
    }
    if (bus->parent) {
        QLIST_REMOVE(bus, sibling);
        bus->parent->num_child_bus--;
    }
    qemu_free((void*)bus->name);
    if (bus->qdev_allocated) {
        qemu_free(bus);
    }
}
