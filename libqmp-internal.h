#ifndef LIBQMP_INTERNAL_H
#define LIBQMP_INTERNAL_H

#include "qemu-objects.h"
#include "qmp-marshal-types.h"
#include "error_int.h"

typedef void (EventTrampolineFunc)(QDict *qmp__args, void *qmp__fn, void *qmp__opaque, Error **qmp__errp);

typedef struct QmpEventTrampoline
{
    const char *name;
    EventTrampolineFunc *dispatch;
    QTAILQ_ENTRY(QmpEventTrampoline) node;
} QmpEventTrampoline;

struct QmpSession
{
    QObject *(*dispatch)(QmpSession *session, const char *name, QDict *args, Error **err);
    bool (*wait_event)(QmpSession *session, struct timeval *tv);
    QTAILQ_HEAD(, QmpEventTrampoline) events;
};

void libqmp_init_events(QmpSession *sess);
void libqmp_register_event(QmpSession *sess, const char *name, EventTrampolineFunc *func);

#endif
