#ifndef QEMU_QDEV_DEVADD_H
#define QEMU_QDEV_DEVADD_H

DeviceState *qdev_device_add(QemuOpts *opts);
int qdev_device_help(QemuOpts *opts);

#endif
