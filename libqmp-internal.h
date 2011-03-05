#ifndef LIBQMP_INTERNAL_H
#define LIBQMP_INTERNAL_H

#include "qemu-objects.h"
#include "qmp-marshal-types.h"
#include "error_int.h"

struct QmpSession
{
    QObject *(*dispatch)(QmpSession *session, const char *name, QDict *args, Error **err);
    bool (*wait_event)(QmpSession *session, struct timeval *tv);
};

#endif
