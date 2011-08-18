/*
 * QEMUFile Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_FILE_OUTPUT_VISITOR_H
#define QEMU_FILE_OUTPUT_VISITOR_H

#include "qapi-visit-core.h"

typedef struct QemuFileOutputVisitor QemuFileOutputVisitor;

QemuFileOutputVisitor *qemu_file_output_visitor_new(QEMUFile *f);
void qemu_file_output_visitor_cleanup(QemuFileOutputVisitor *d);

Visitor *qemu_file_output_get_visitor(QemuFileOutputVisitor *v);

#endif
