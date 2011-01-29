#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "monitor.h"

typedef int (QmpCommandFunc)(Monitor *, const QDict *, QObject **);

void qmp_register_command(const char *name, QmpCommandFunc *fn);

#endif

