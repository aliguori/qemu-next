#ifndef LIBQMP_CORE_H
#define LIBQMP_CORE_H

#include "qemu-objects.h"
#include "qmp-types.h"
#include "error.h"

typedef struct QmpSession QmpSession;

QmpSession *qmp_session_new(int fd);
void qmp_session_destroy(QmpSession *s);

#endif
