#ifndef QMP_OUTPUT_VISITER_H
#define QMP_OUTPUT_VISITER_H

#include "qapi-visit-core.h"
#include "qobject.h"

typedef struct QmpOutputVisiter QmpOutputVisiter;

QmpOutputVisiter *qmp_output_visiter_new(void);

QObject *qmp_output_get_qobject(QmpOutputVisiter *v);
Visiter *qmp_output_get_visiter(QmpOutputVisiter *v);

#endif
