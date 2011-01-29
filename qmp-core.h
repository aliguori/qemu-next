#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "monitor.h"

typedef void (QmpCommandFunc)(const QDict *, QObject **, Error **);

void qmp_register_command(const char *name, QmpCommandFunc *fn);
void qmp_init_chardev(CharDriverState *chr);

#endif

