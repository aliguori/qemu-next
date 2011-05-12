#ifndef QDEV_DOC_H
#define QDEV_DOC_H

#include "qemu-common.h"

typedef struct PropertyDocumentation
{
    const char *name;
    const char *type;
    const char *docs;
} PropertyDocumentation;

typedef struct DeviceStateDocumentation
{
    const char *name;
    PropertyDocumentation *properties;
} DeviceStateDocumentation;

extern DeviceStateDocumentation device_docs[];

bool qdev_verify_docs(void);

#endif
