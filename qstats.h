/*
 * QEMU statistics support
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * NB: These statistics reflect internal QEMU data structures and are not guaranteed to be
 * consistent across QEMU versions.  YMMV.
 */

#ifndef QSTATS_H
#define QSTATS_H

#include "qemu_common.h"
#include "monitor.h"

#define STRIFY_I(var) # var
#define STRIFY(var) STRIFY_I(var)

#define QSTAT_DECLARE(var)  uint64_t var
#define QSTAT_DESC(type, var) .offset = offsetof(type, var), .name = STRIFY(var)

typedef struct QStatDescription
{
    size_t offset;
    const char *name;
    const char *description;
} QStatDescription;

void do_stat(Monitor *mon, const char *class, int has_instance, int instance_id);

int register_qstat(const char *class, int instance_id, QStatDescription *descs, int n_descs, void *stats);
void unregister_qstat(const char *class, int instance_id);

#endif
