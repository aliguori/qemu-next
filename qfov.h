#ifndef QFOV_H
#define QFOV_H

#include "qapi/qapi-visit-core.h"
#include "hw/hw.h"

typedef struct QEMUFileOutputVisitor QEMUFileOutputVisitor;

QEMUFileOutputVisitor *qemu_file_output_visitor_new(QEMUFile *f);
Visitor *qemu_file_output_get_visitor(QEMUFileOutputVisitor *qfov);
Visitor *output_visitor_from_qemu_file(QEMUFile *f);

#endif
