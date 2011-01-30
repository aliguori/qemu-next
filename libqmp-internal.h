#ifndef LIBQMP_INTERNAL_H
#define LIBQMP_INTERNAL_H

#include "qemu-objects.h"

struct QmpSession
{
    QObject *(*dispatch)(QmpSession *session, const char *name, QDict *args, Error **err);
};

#endif
