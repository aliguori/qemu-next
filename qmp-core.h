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

#endif
