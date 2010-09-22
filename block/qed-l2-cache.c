/*
 * QEMU Enhanced Disk Format L2 Cache
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qed.h"

/* Each L2 holds 2GB so this let's us fully cache a 100GB disk */
#define MAX_L2_CACHE_SIZE 50

/**
 * Initialize the L2 cache
 */
void qed_init_l2_cache(L2TableCache *l2_cache,
                       L2TableAllocFunc *alloc_l2_table,
                       void *alloc_l2_table_opaque)
{
    QTAILQ_INIT(&l2_cache->entries);
    l2_cache->n_entries = 0;
    l2_cache->alloc_l2_table = alloc_l2_table;
    l2_cache->alloc_l2_table_opaque = alloc_l2_table_opaque;
}

/**
 * Free the L2 cache
 */
void qed_free_l2_cache(L2TableCache *l2_cache)
{
    CachedL2Table *entry, *next_entry;

    QTAILQ_FOREACH_SAFE(entry, &l2_cache->entries, node, next_entry) {
        qemu_vfree(entry->table);
        qemu_free(entry);
    }
}

/**
 * Allocate an uninitialized entry from the cache
 *
 * The returned entry has a reference count of 1 and is owned by the caller.
 */
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache)
{
    CachedL2Table *entry;

    entry = qemu_mallocz(sizeof(*entry));
    entry->table = l2_cache->alloc_l2_table(l2_cache->alloc_l2_table_opaque);
    entry->ref++;

    return entry;
}

/**
 * Decrease an entry's reference count and free if necessary when the reference
 * count drops to zero.
 */
void qed_unref_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *entry)
{
    if (!entry) {
        return;
    }

    entry->ref--;
    if (entry->ref == 0) {
        qemu_vfree(entry->table);
        qemu_free(entry);
    }
}

/**
 * Find an entry in the L2 cache.  This may return NULL and it's up to the
 * caller to satisfy the cache miss.
 *
 * For a cached entry, this function increases the reference count and returns
 * the entry.
 */
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset)
{
    CachedL2Table *entry;

    QTAILQ_FOREACH(entry, &l2_cache->entries, node) {
        if (entry->offset == offset) {
            entry->ref++;
            return entry;
        }
    }
    return NULL;
}

/**
 * Commit an L2 cache entry into the cache.  This is meant to be used as part of
 * the process to satisfy a cache miss.  A caller would allocate an entry which
 * is not actually in the L2 cache and then once the entry was valid and
 * present on disk, the entry can be committed into the cache.
 *
 * Since the cache is write-through, it's important that this function is not
 * called until the entry is present on disk and the L1 has been updated to
 * point to the entry.
 *
 * N.B. This function steals a reference to the l2_table from the caller so the
 * caller must obtain a new reference by issuing a call to
 * qed_find_l2_cache_entry().
 */
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table)
{
    CachedL2Table *entry;

    entry = qed_find_l2_cache_entry(l2_cache, l2_table->offset);
    if (entry) {
        qed_unref_l2_cache_entry(l2_cache, entry);
        qed_unref_l2_cache_entry(l2_cache, l2_table);
        return;
    }

    if (l2_cache->n_entries >= MAX_L2_CACHE_SIZE) {
        entry = QTAILQ_FIRST(&l2_cache->entries);
        QTAILQ_REMOVE(&l2_cache->entries, entry, node);
        l2_cache->n_entries--;
        qed_unref_l2_cache_entry(l2_cache, entry);
    }

    l2_cache->n_entries++;
    QTAILQ_INSERT_TAIL(&l2_cache->entries, l2_table, node);
}
