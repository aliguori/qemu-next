#ifndef QEMU_TOOL_H
#define QEMU_TOOL_H

#include "qemu-common.h"

#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

typedef void VMStateDescription;
typedef int VMStateInfo;

#ifndef _WIN32
void qemu_event_increment(void);
int qemu_event_init(void);
#else
int qemu_event_init(void);
void qemu_event_increment(void);
#endif

void qemu_put_be64(QEMUFile *f, uint64_t v);
uint64_t qemu_get_be64(QEMUFile *f);
int vmstate_register(DeviceState *dev, int instance_id,
                     const VMStateDescription *vmsd, void *opaque);

#endif
