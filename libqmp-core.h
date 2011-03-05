#ifndef LIBQMP_CORE_H
#define LIBQMP_CORE_H

#include <sys/types.h>
#include "qmp-types.h"
#include "error.h"

typedef struct QmpSession QmpSession;

QmpSession *qmp_session_new(int fd);
void qmp_session_destroy(QmpSession *s);

bool libqmp_poll_event(QmpSession *s);
bool libqmp_wait_event(QmpSession *s, struct timeval *tv);

GuestInfo *libqmp_list_guests(void);
QmpSession *libqmp_session_new_name(const char *name);
QmpSession *libqmp_session_new_pid(pid_t pid);
void *libqmp_signal_new(QmpSession *s, size_t size, int global_handle);
int libqmp_signal_connect(QmpSignal *obj, void *func, void *opaque);
void libqmp_signal_disconnect(QmpSignal *obj, int handle);
void libqmp_signal_free(void *base, QmpSignal *obj);

#define libqmp_signal_init(s, type, global_handle) \
    ((type *)libqmp_signal_new(s, sizeof(type), global_handle))

#define signal_connect(obj, fn, opaque) \
    libqmp_signal_connect((obj)->signal, (obj)->func = fn, opaque)

#define signal_disconnect(obj, handle) \
    libqmp_signal_disconnect((obj)->signal, handle)

#define signal_unref(obj) \
    libqmp_signal_free((obj), (obj)->signal)

#endif
