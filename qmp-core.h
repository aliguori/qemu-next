#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "monitor.h"
#include "qmp-marshal-types.h"
#include "error_int.h"

typedef void (QmpCommandFunc)(const QDict *, QObject **, Error **);

void qmp_register_command(const char *name, QmpCommandFunc *fn);
void qmp_init_chardev(CharDriverState *chr);

typedef struct QmpUnixServer QmpUnixServer;
QmpUnixServer *qmp_unix_server_new(const char *path);
void qmp_unix_server_delete(QmpUnixServer *path);

char *qobject_as_string(QObject *obj);

typedef struct QmpSlot
{
    int handle;
    void *func;
    void *opaque;
    QTAILQ_ENTRY(QmpSlot) node;
} QmpSlot;

struct QmpSignal
{
    int max_handle;
    QTAILQ_HEAD(, QmpSlot) slots;
};

QmpSignal *qmp_signal_init(void);
void qmp_signal_delete(QmpSignal *obj);
int qmp_signal_connect(QmpSignal *obj, void *func, void *opaque);
void qmp_signal_disconnect(QmpSignal *obj, int handle);

#define signal_init(obj) do {          \
    (obj)->signal = qmp_signal_init(); \
} while (0)

#define signal_connect(obj, func, opaque) \
    qmp_signal_connect((obj)->signal, (obj)->func = func, opaque)

#define signal_disconnect(obj, handle) \
    qmp_signal_disconnect((obj)->signal, handle)

#define signal_notify(obj, ...) do {                       \
    QmpSlot *qmp__slot;                                    \
    QTAIQ_FOREACH(qmp__slot, (obj)->signal.slots, node) {  \
        (obj)->func = slot->func;                          \
        (obj)->func(slot->opaque, ## __VA_ARGS__);            \
    }                                                      \
}

#endif

