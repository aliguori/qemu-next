#ifndef QEMU_QDEV_DEVADD_H
#define QEMU_QDEV_DEVADD_H

#include "qdev.h"
#include "qemu-option.h"

DeviceState *qdev_device_add(QemuOpts *opts);
int qdev_device_help(QemuOpts *opts);
int qdev_device_add_is_black_listed(const char *id);

#endif
