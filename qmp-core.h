/*
 * QAPI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "monitor.h"
#include "qmp-marshal-types.h"
#include "error_int.h"

typedef struct QmpState QmpState;

typedef void (QmpCommandFunc)(const QDict *, QObject **, Error **);
typedef void (QmpStatefulCommandFunc)(QmpState *qmp__sess, const QDict *, QObject **, Error **);

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
    int ref;
    QTAILQ_HEAD(, QmpSlot) slots;
};

typedef struct QmpConnection
{
    QmpState *state;
    const char *event_name;
    QmpSignal *signal;
    int handle;
    int global_handle;
    QTAILQ_ENTRY(QmpConnection) node;
} QmpConnection;

void qmp_register_command(const char *name, QmpCommandFunc *fn);
void qmp_register_stateful_command(const char *name, QmpStatefulCommandFunc *fn);
char *qobject_as_string(QObject *obj);

QmpSignal *qmp_signal_init(void);
void qmp_signal_ref(QmpSignal *obj);
void qmp_signal_unref(QmpSignal *obj);
int qmp_signal_connect(QmpSignal *obj, void *func, void *opaque);
void qmp_signal_disconnect(QmpSignal *obj, int handle);

void qmp_state_add_connection(QmpState *sess, const char *name, QmpSignal *obj, int handle, QmpConnection *conn);
void qmp_state_del_connection(QmpState *sess, int global_handle, Error **errp);
void qmp_state_event(QmpConnection *conn, QObject *data);

#define signal_init(obj) do {          \
    (obj)->signal = qmp_signal_init(); \
} while (0)

#define signal_unref(obj) qmp_signal_unref((obj)->signal)

#define signal_connect(obj, fn, opaque) \
    qmp_signal_connect((obj)->signal, (obj)->func = fn, opaque)

#define signal_disconnect(obj, handle) \
    qmp_signal_disconnect((obj)->signal, handle)

#define signal_notify(obj, ...) do {                         \
    QmpSlot *qmp__slot;                                      \
    QTAILQ_FOREACH(qmp__slot, &(obj)->signal->slots, node) { \
        (obj)->func = qmp__slot->func;                       \
        (obj)->func(qmp__slot->opaque, ## __VA_ARGS__);      \
    }                                                        \
} while(0)

void qmp_init_chardev(CharDriverState *chr);

#endif
