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

#ifndef QEMU_FILE_INPUT_VISITOR_H
#define QEMU_FILE_INPUT_VISITOR_H

#include "qapi-visit-core.h"

typedef struct QemuFileInputVisitor QemuFileInputVisitor;

QemuFileInputVisitor *qemu_file_input_visitor_new(QEMUFile *f);
void qemu_file_input_visitor_cleanup(QemuFileInputVisitor *d);

Visitor *qemu_file_input_get_visitor(QemuFileInputVisitor *v);

#endif
