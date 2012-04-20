/* QEMU Synchronous Serial Interface support.  */

/* In principle SSI is a point-point interface.
   However it is fairly common for boards to have multiple slaves
   connected to a single master, and select devices with an external
   chip select. SSI busses can therefore have any number of slaves,
   of which a master can select using ssi_select_slave().
   It is assumed that master and slave are both using the same transfer width.
   */

#ifndef QEMU_SSI_H
#define QEMU_SSI_H

#include "qdev.h"

typedef struct SSISlave SSISlave;

#define TYPE_SSI_SLAVE "ssi-slave"
#define SSI_SLAVE(obj) \
     OBJECT_CHECK(SSISlave, (obj), TYPE_SSI_SLAVE)
#define SSI_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SSISlaveClass, (klass), TYPE_SSI_SLAVE)
#define SSI_SLAVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SSISlaveClass, (obj), TYPE_SSI_SLAVE)

/* Slave devices.  */
typedef struct SSISlaveClass {
    DeviceClass parent_class;

    int (*init)(SSISlave *dev);
    uint32_t (*transfer)(SSISlave *dev, uint32_t val);
    int (*set_cs)(SSISlave *dev, int state);
} SSISlaveClass;

struct SSISlave {
    DeviceState qdev;

    int32_t ss;
};

#define SSI_SLAVE_FROM_QDEV(dev) DO_UPCAST(SSISlave, qdev, dev)
#define FROM_SSI_SLAVE(type, dev) DO_UPCAST(type, ssidev, dev)

DeviceState *ssi_create_slave(SSIBus *bus, const char *name, int32_t ss);

/* Master interface.  */
SSIBus *ssi_create_bus(DeviceState *parent, const char *name);

#define SSI_SLAVE_SELECT_NONE (-1)

void ssi_select_slave(SSIBus *bus, int32_t ss);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

extern const VMStateDescription vmstate_ssi_slave;

#define VMSTATE_SSI_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SSISlave),                                  \
    .vmsd       = &vmstate_ssi_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SSISlave),    \
}

/* max111x.c */
void max111x_set_input(DeviceState *dev, int line, uint8_t value);

#endif
