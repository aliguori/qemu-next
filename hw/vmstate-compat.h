#ifndef QEMU_VMSTATE_COMPAT_H
#define QEMU_VMSTATE_COMPAT_H

#include "vmstate.h"

extern const VMStateDescription vmstate_pci_device;

#define VMSTATE_PCI_DEVICE(_field, _state) {                         \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pci_device,                               \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PCIDevice),   \
}

extern const VMStateDescription vmstate_pcie_device;

#define VMSTATE_PCIE_DEVICE(_field, _state) {                        \
    .name       = (stringify(_field)),                               \
    .version_id = 2,                                                 \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pcie_device,                              \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PCIDevice),   \
}

extern const VMStateDescription vmstate_i2c_slave;

#define VMSTATE_I2C_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(i2c_slave),                                 \
    .vmsd       = &vmstate_i2c_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, i2c_slave),   \
}

#define vmstate_offset_macaddr(_state, _field)                       \
    vmstate_offset_array(_state, _field.a, uint8_t,                \
                         sizeof(typeof_field(_state, _field)))

#define VMSTATE_MACADDR(_field, _state) {                            \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(MACAddr),                                   \
    .info       = "buffer",                                          \
    .flags      = VMS_BUFFER,                                        \
    .offset     = vmstate_offset_macaddr(_state, _field),            \
}

#define VMSTATE_TIMER_V(_f, _s, _v)                                   \
    VMSTATE_POINTER(_f, _s, _v, "timer", QEMUTimer *)

#define VMSTATE_TIMER(_f, _s)                                         \
    VMSTATE_TIMER_V(_f, _s, 0)

#define VMSTATE_TIMER_ARRAY(_f, _s, _n)                              \
    VMSTATE_ARRAY_OF_POINTER(_f, _s, _n, 0, "timer", QEMUTimer *)

#define VMSTATE_PTIMER_V(_f, _s, _v)                                  \
    VMSTATE_POINTER(_f, _s, _v, "ptimer", ptimer_state *)

#define VMSTATE_PTIMER(_f, _s)                                        \
    VMSTATE_PTIMER_V(_f, _s, 0)

#endif
