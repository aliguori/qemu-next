/*
 * QEMU Enhanced Disk Format
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "block.h"

/**
 * Generic callback for chaining async callbacks
 */
typedef struct {
    BlockDriverCompletionFunc *cb;
    void *opaque;
} GenericCB;

void *gencb_alloc(size_t len, BlockDriverCompletionFunc *cb, void *opaque);
void gencb_complete(void *opaque, int ret);
