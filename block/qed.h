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

#ifndef BLOCK_QED_H
#define BLOCK_QED_H

#include "block_int.h"

/* The layout of a QED file is as follows:
 *
 * +--------+----------+----------+----------+-----+
 * | header | L1 table | cluster0 | cluster1 | ... |
 * +--------+----------+----------+----------+-----+
 *
 * There is a 2-level pagetable for cluster allocation:
 *
 *                     +----------+
 *                     | L1 table |
 *                     +----------+
 *                ,------'  |  '------.
 *           +----------+   |    +----------+
 *           | L2 table |  ...   | L2 table |
 *           +----------+        +----------+
 *       ,------'  |  '------.
 *  +----------+   |    +----------+
 *  |   Data   |  ...   |   Data   |
 *  +----------+        +----------+
 *
 * The L1 table is fixed size and always present.  L2 tables are allocated on
 * demand.  The L1 table size determines the maximum possible image size; it
 * can be influenced using the cluster_size and table_size values.
 *
 * All fields are little-endian on disk.
 */

typedef struct {
    uint32_t magic;                 /* QED */

    uint32_t cluster_size;          /* in bytes */
    uint32_t table_size;            /* table size, in clusters */
    uint32_t first_cluster;         /* first usable cluster */

    uint64_t features;              /* format feature bits */
    uint64_t compat_features;       /* compatible feature bits */
    uint64_t l1_table_offset;       /* L1 table offset, in bytes */
    uint64_t image_size;            /* total image size, in bytes */

    uint32_t backing_file_offset;   /* in bytes from start of header */
    uint32_t backing_file_size;     /* in bytes */
    uint32_t backing_fmt_offset;    /* in bytes from start of header */
    uint32_t backing_fmt_size;      /* in bytes */
} QEDHeader;

typedef struct {
    uint64_t offsets[0];            /* in bytes */
} QEDTable;

/* The L2 cache is a simple write-through cache for L2 structures */
typedef struct CachedL2Table {
    QEDTable *table;
    uint64_t offset;    /* offset=0 indicates an invalidate entry */
    QTAILQ_ENTRY(CachedL2Table) node;
    int ref;
} CachedL2Table;

/**
 * Allocate an L2 table
 *
 * This callback is used by the L2 cache to allocate tables without knowing
 * their size or alignment requirements.
 */
typedef QEDTable *L2TableAllocFunc(void *opaque);

typedef struct {
    QTAILQ_HEAD(, CachedL2Table) entries;
    unsigned int n_entries;
    L2TableAllocFunc *alloc_l2_table;
    void *alloc_l2_table_opaque;
} L2TableCache;

typedef struct QEDRequest {
    CachedL2Table *l2_table;
} QEDRequest;

typedef struct QEDAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int bh_ret;                     /* final return status for completion bh */
    QSIMPLEQ_ENTRY(QEDAIOCB) next; /* next request */
    bool is_write;                  /* false - read, true - write */

    /* User scatter-gather list */
    QEMUIOVector *qiov;
    struct iovec *cur_iov;          /* current iovec to process */
    size_t cur_iov_offset;          /* byte count already processed in iovec */

    /* Current cluster scatter-gather list */
    QEMUIOVector cur_qiov;
    uint64_t cur_pos;               /* position on block device, in bytes */
    uint64_t end_pos;
    uint64_t cur_cluster;           /* cluster offset in image file */
    unsigned int cur_nclusters;     /* number of clusters being accessed */
    int find_cluster_ret;           /* used for L1/L2 update */

    QEDRequest request;
} QEDAIOCB;

typedef struct {
    BlockDriverState *bs;           /* device */
    uint64_t file_size;             /* length of image file, in bytes */

    QEDHeader header;              /* always cpu-endian */
    QEDTable *l1_table;
    L2TableCache l2_cache;          /* l2 table cache */
    uint32_t table_nelems;
    uint32_t l1_shift;
    uint32_t l2_shift;
    uint32_t l2_mask;

    /* Pending request queue */
    QSIMPLEQ_HEAD(, QEDAIOCB) reqs;
} BDRVQEDState;

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
 * L2 cache functions
 */
void qed_init_l2_cache(L2TableCache *l2_cache, L2TableAllocFunc *alloc_l2_table, void *alloc_l2_table_opaque);
void qed_free_l2_cache(L2TableCache *l2_cache);
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache);
void qed_free_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *entry);
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset);
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table);

/**
 * Table I/O functions
 */
int qed_read_l1_table(BDRVQEDState *s);
void qed_write_l1_table(BDRVQEDState *s, unsigned int index, unsigned int n,
                        BlockDriverCompletionFunc *cb, void *opaque);
void qed_read_l2_table(BDRVQEDState *s, QEDRequest *request, uint64_t offset,
                       BlockDriverCompletionFunc *cb, void *opaque);
void qed_write_l2_table(BDRVQEDState *s, QEDRequest *request,
                        unsigned int index, unsigned int n, bool flush,
                        BlockDriverCompletionFunc *cb, void *opaque);
#endif /* BLOCK_QED_H */
