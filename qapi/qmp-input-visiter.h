#ifndef QMP_INPUT_VISITER_H
#define QMP_INPUT_VISITER_H

#include "qapi-visit-core.h"
#include "qobject.h"

typedef struct QmpInputVisiter QmpInputVisiter;

QmpInputVisiter *qmp_input_visiter_new(QObject *obj);

Visiter *qmp_input_get_visiter(QmpInputVisiter *v);

#endif
