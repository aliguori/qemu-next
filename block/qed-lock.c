/*
 * QEMU Enhanced Disk Format Lock
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * Table locking works as follows:
 *
 * Reads and non-allocating writes do not acquire locks because they do not
 * modify tables and only see committed L2 cache entries.
 *
 * An allocating write request that needs to update an existing L2 table
 * acquires a lock on the table.  This serializes requests that touch the same
 * L2 table.
 *
 * An allocating write request that needs to create a new L2 table and update
 * the L1 table acquires a lock on the L1 table.  This serializes requests that
 * create new L2 tables.
 *
 * When a request is unable to acquire a lock, it is put to sleep and must
 * return.  When the lock it attempted to acquire becomes available, a wakeup
 * function is invoked to activate it again.
 *
 * A request must retry its cluster lookup after waking up because the tables
 * have changed.  For example, an allocating write may no longer need to
 * allocate if the previous request already allocated the cluster.
 */

#include "qed.h"

struct QEDLockEntry {
    uint64_t key;
    QSIMPLEQ_HEAD(, QEDAIOCB) reqs;
    QTAILQ_ENTRY(QEDLockEntry) next;
};

/**
 * Initialize a lock
 *
 * @lock:           Lock
 * @wakeup_fn:      Callback to reactivate a sleeping request
 */
void qed_lock_init(QEDLock *lock, BlockDriverCompletionFunc *wakeup_fn)
{
    QTAILQ_INIT(&lock->entries);
    lock->wakeup_fn = wakeup_fn;
}

/**
 * Acquire a lock on a given key
 *
 * @lock:           Lock
 * @key:            Key to lock on
 * @acb:            Request
 * @ret:            true if lock was acquired, false if request needs to sleep
 *
 * If the request currently has another lock held, that lock will be released.
 */
bool qed_lock(QEDLock *lock, uint64_t key, QEDAIOCB *acb)
{
    QEDLockEntry *entry = acb->lock_entry;

    if (entry) {
        /* Lock already held */
        if (entry->key == key) {
            return true;
        }

        /* Release old lock */
        qed_unlock(lock, acb);
    }

    /* Find held lock */
    QTAILQ_FOREACH(entry, &lock->entries, next) {
        if (entry->key == key) {
            QSIMPLEQ_INSERT_TAIL(&entry->reqs, acb, next);
            acb->lock_entry = entry;
            return false;
        }
    }

    /* Allocate new lock entry */
    entry = qemu_malloc(sizeof(*entry));
    entry->key = key;
    QSIMPLEQ_INIT(&entry->reqs);
    QSIMPLEQ_INSERT_TAIL(&entry->reqs, acb, next);
    QTAILQ_INSERT_TAIL(&lock->entries, entry, next);
    acb->lock_entry = entry;
    return true;
}

/**
 * Release a held lock
 */
void qed_unlock(QEDLock *lock, QEDAIOCB *acb)
{
    QEDLockEntry *entry = acb->lock_entry;

    if (!entry) {
        return;
    }

    acb->lock_entry = NULL;
    QSIMPLEQ_REMOVE_HEAD(&entry->reqs, next);

    /* Wake up next lock holder */
    acb = QSIMPLEQ_FIRST(&entry->reqs);
    if (acb) {
        lock->wakeup_fn(acb, 0);
        return;
    }

    /* Free lock entry */
    QTAILQ_REMOVE(&lock->entries, entry, next);
    qemu_free(entry);
}
