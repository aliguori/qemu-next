#ifndef QMP_MARSHAL_TYPES_CORE_H
#define QMP_MARSHAL_TYPES_CORE_H

#include "qemu-common.h"
#include "qemu-objects.h"
#include "qerror.h"
#include "error.h"
#include "qapi-types.h"

typedef struct QmpMarshalState QmpMarshalState;

struct QmpMarshalState
{
    bool non_canonical_handles;
};

QObject *qmp_marshal_type_int(QmpMarshalState *qmp__mstate, int64_t value);
QObject *qmp_marshal_type_str(QmpMarshalState *qmp__mstate, const char *value);
QObject *qmp_marshal_type_bool(QmpMarshalState *qmp__mstate, bool value);
QObject *qmp_marshal_type_number(QmpMarshalState *qmp__mstate, double value);

int64_t qmp_unmarshal_type_int(QmpMarshalState *qmp__mstate, QObject *value, Error **errp);
char *qmp_unmarshal_type_str(QmpMarshalState *qmp__mstate, QObject *value, Error **errp);
bool qmp_unmarshal_type_bool(QmpMarshalState *qmp__mstate, QObject *value, Error **errp);
double qmp_unmarshal_type_number(QmpMarshalState *qmp__mstate, QObject *value, Error **errp);

#endif
