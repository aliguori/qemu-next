#ifndef LIBQMP_CORE_H
#define LIBQMP_CORE_H

#include <sys/types.h>
#include "qemu-objects.h"
#include "qmp-types.h"
#include "error.h"

typedef struct QmpSession QmpSession;

QmpSession *qmp_session_new(int fd);
void qmp_session_destroy(QmpSession *s);

GuestInfo *libqmp_list_guests(void);
QmpSession *libqmp_session_new_name(const char *name);
QmpSession *libqmp_session_new_pid(pid_t pid);

#endif
