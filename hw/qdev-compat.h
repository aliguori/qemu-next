#ifndef QEMU_QDEV_COMPAT_H
#define QEMU_QDEV_COMPAT_H

#include "qdev.h"
#include "net.h"
#include "blockdev.h"
#include "qemu-char.h"

/* Legacy helpers for initializing device properties */

CharDriverState *qdev_init_chardev(DeviceState *dev);

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd);

BlockDriverState *qdev_init_bdrv(DeviceState *dev, BlockInterfaceType type);

#endif
