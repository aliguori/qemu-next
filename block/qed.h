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

/**
 * L2 Cache
 */

typedef struct QEDTable QEDTable;

/**
 * Allocate an L2 table
 *
 * This callback is used by the L2 cache to allocate tables without knowing
 * their size or alignment requirements.
 */
typedef QEDTable *L2TableAllocFunc(void *opaque);

/* The L2 cache is a simple write-through cache for L2 structures */
typedef struct CachedL2Table {
    QEDTable *table;
    uint64_t offset;    /* offset=0 indicates an invalidate entry */
    QTAILQ_ENTRY(CachedL2Table) node;
    int ref;
} CachedL2Table;

typedef struct {
    QTAILQ_HEAD(, CachedL2Table) entries;
    unsigned int n_entries;
    L2TableAllocFunc *alloc_l2_table;
    void *alloc_l2_table_opaque;
} L2TableCache;

void qed_init_l2_cache(L2TableCache *l2_cache, L2TableAllocFunc *alloc_l2_table, void *alloc_l2_table_opaque);
void qed_free_l2_cache(L2TableCache *l2_cache);
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache);
void qed_free_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *entry);
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset);
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table);
