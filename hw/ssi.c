/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "ssi.h"

struct SSIBus {
    BusState qbus;
    int32_t ss;
};

static struct BusInfo ssi_bus_info = {
    .name = "SSI",
    .size = sizeof(SSIBus),
    .props = (Property[]) {
        DEFINE_PROP_INT32("ss", struct SSISlave, ss, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_ssi_bus = {
    .name = "ssi_bus",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(ss, SSIBus),
        VMSTATE_END_OF_LIST()
    }
};

static int ssi_slave_init(DeviceState *dev)
{
    SSISlave *s = SSI_SLAVE(dev);
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);

    return ssc->init(s);
}

static void ssi_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = ssi_slave_init;
    dc->bus_info = &ssi_bus_info;
}

static TypeInfo ssi_slave_info = {
    .name = TYPE_SSI_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(struct SSISlave),
    .class_init = ssi_slave_class_init,
    .class_size = sizeof(SSISlaveClass),
    .abstract = true,
};

DeviceState *ssi_create_slave(SSIBus *bus, const char *name, int32_t ss)
{
    DeviceState *dev;
    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_int32(dev, "ss", ss);
    qdev_init_nofail(dev);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    SSIBus *bus;

    bus = FROM_QBUS(SSIBus, qbus_create(&ssi_bus_info, parent, name));
    vmstate_register(NULL, -1, &vmstate_ssi_bus, bus);
    return  bus;
}

static SSISlave *get_current_slave(SSIBus *bus)
{
    DeviceState *qdev;

    QTAILQ_FOREACH(qdev, &bus->qbus.children, sibling) {
        SSISlave *candidate = SSI_SLAVE_FROM_QDEV(qdev);
        if (candidate->ss == bus->ss) {
            return candidate;
        }
    }

    return NULL;
}

void ssi_select_slave(SSIBus *bus, int32_t ss)
{
    SSISlave *slave;
    SSISlaveClass *ssc;

    if (bus->ss == ss) {
        return;
    }

    slave = get_current_slave(bus);
    if (slave) {
        ssc = SSI_SLAVE_GET_CLASS(slave);
        if (ssc->set_cs) {
            ssc->set_cs(slave, 0);
        }
    }
    bus->ss = ss;

    slave = get_current_slave(bus);
    if (slave) {
        ssc = SSI_SLAVE_GET_CLASS(slave);
        if (ssc->set_cs) {
            ssc->set_cs(slave, 1);
        }
    }

}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    SSISlave *slave;
    SSISlaveClass *ssc;

    slave = get_current_slave(bus);
    if (!slave) {
        return 0;
    }
    ssc = SSI_SLAVE_GET_CLASS(slave);
    return ssc->transfer(slave, val);
}

const VMStateDescription vmstate_ssi_slave = {
    .name = "SSISlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(ss, SSISlave),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_slave_register_types(void)
{
    type_register_static(&ssi_slave_info);
}

type_init(ssi_slave_register_types)
