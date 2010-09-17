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

#include "qemu-common.h"
#include "block.h"
#include "qed.h"

void qed_rq_init(RequestQueue *rq)
{
    QTAILQ_INIT(rq);
}

static void qed_rq_next(RequestQueue *rq)
{
    RequestQueueEntry *rqe;

    if (QTAILQ_EMPTY(rq)) {
        return;
    }

    rqe = QTAILQ_FIRST(rq);
    rqe->cb(rqe->opaque, 0);
}

bool qed_rq_is_entry_active(RequestQueue *rq, RequestQueueEntry *rqe)
{
    return (rqe == QTAILQ_FIRST(rq));
}

RequestQueueEntry *qed_rq_enqueue_nostart(RequestQueue *rq,
                                          BlockDriverCompletionFunc *cb,
                                          void *opaque)
{
    RequestQueueEntry *rqe;

    rqe = qemu_malloc(sizeof(*rqe));
    rqe->cb = cb;
    rqe->opaque = opaque;

    QTAILQ_INSERT_TAIL(rq, rqe, next);

    return rqe;
}

RequestQueueEntry *qed_rq_enqueue(RequestQueue *rq,
                                  BlockDriverCompletionFunc *cb,
                                  void *opaque)
{
    bool autostart = QTAILQ_EMPTY(rq);
    RequestQueueEntry *rqe;

    rqe = qed_rq_enqueue_nostart(rq, cb, opaque);
    if (autostart) {
        qed_rq_next(rq);
    }

    return rqe;
}

void qed_rq_dequeue(RequestQueue *rq, RequestQueueEntry *rqe)
{
    if (rqe == QTAILQ_FIRST(rq)) {
        QTAILQ_REMOVE(rq, rqe, next);
        qed_rq_next(rq);
    } else {
        QTAILQ_REMOVE(rq, rqe, next);
    }
    qemu_free(rqe);
}

void qed_rq_destroy(RequestQueue *rq)
{
    RequestQueueEntry *rqe, *next_rqe;

    QTAILQ_FOREACH_SAFE(rqe, rq, next, next_rqe) {
        QTAILQ_REMOVE(rq, rqe, next);
        qemu_free(rqe);
    }
}
