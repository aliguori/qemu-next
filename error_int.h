#ifndef QEMU_ERROR_INT_H
#define QEMU_ERROR_INT_H

#include "qemu-common.h"
#include "qobject.h"
#include "error.h"

/* FIXME make this a hidden internal API */
QObject *error_get_qobject(Error *err);
void error_set_qobject(Error **errp, QObject *obj);
  
#endif
