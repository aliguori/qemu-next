#ifndef LIBQMP_CORE_H
#define LIBQMP_CORE_H

#include "qemu-objects.h"

struct QmpSession
{
    QObject *(*dispatch)(QmpSession *session, const char *name, const QDict *args, Error **err);
};

#endif
