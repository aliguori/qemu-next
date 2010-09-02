/*
 * Block protocol for block driver correctness testing
 *
 * Copyright (C) 2010 IBM, Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdarg.h>
#include "block_int.h"

typedef struct {
    BlockDriverState *test_file;
} BDRVBlkverifyState;

typedef struct BlkverifyAIOCB BlkverifyAIOCB;
struct BlkverifyAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;

    /* Request metadata */
    bool is_write;
    int64_t sector_num;
    int nb_sectors;

    int ret;                    /* first completed request's result */
    unsigned int done;          /* completion counter */

    QEMUIOVector *qiov;         /* user I/O vector */
    QEMUIOVector raw_qiov;      /* cloned I/O vector for raw file */
    void *buf;                  /* buffer for raw file I/O */

    void (*verify)(BlkverifyAIOCB *acb);
};

static void blkverify_aio_cancel_cb(void *opaque, int ret)
{
    *(bool *)opaque = true;
}

static void blkverify_aio_cancel(BlockDriverAIOCB *acb)
{
    bool done = false;

    /* Allow the request to complete so the raw file and test file stay in
     * sync.  Hijack the callback (since the request is cancelled anyway) to
     * wait for completion.
     */
    acb->cb = blkverify_aio_cancel_cb;
    acb->opaque = &done;
    while (!done) {
        qemu_aio_wait();
    }
}

static AIOPool blkverify_aio_pool = {
    .aiocb_size         = sizeof(BlkverifyAIOCB),
    .cancel             = blkverify_aio_cancel,
};

static void blkverify_err(BlkverifyAIOCB *acb, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "blkverify: %s sector_num=%ld nb_sectors=%d ",
            acb->is_write ? "write" : "read", acb->sector_num,
            acb->nb_sectors);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Valid blkverify filenames look like blkverify:path/to/raw_image:path/to/image */
static int blkverify_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVBlkverifyState *s = bs->opaque;
    int ret;
    char *raw, *c;

    /* Parse the blkverify: prefix */
    if (strncmp(filename, "blkverify:", strlen("blkverify:"))) {
        return -EINVAL;
    }
    filename += strlen("blkverify:");

    /* Parse the raw image filename */
    c = strchr(filename, ':');
    if (c == NULL) {
        return -EINVAL;
    }

    raw = strdup(filename);
    raw[c - filename] = '\0';
    ret = bdrv_file_open(&bs->file, raw, flags);
    free(raw);
    if (ret < 0) {
        return ret;
    }
    filename = c + 1;

    /* Open the test file */
    s->test_file = bdrv_new("");
    ret = bdrv_open(s->test_file, filename, flags, NULL);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void blkverify_close(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    bdrv_delete(s->test_file);
    s->test_file = NULL;
}

static void blkverify_flush(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    /* Only flush test file, the raw file is not important */
    bdrv_flush(s->test_file);
}

static int64_t blkverify_getlength(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    return bdrv_getlength(s->test_file);
}

/**
 * Check that I/O vector contents are identical
 *
 * @a:          I/O vector
 * @b:          I/O vector
 * @ret:        Offset to first mismatching byte or -1 if match
 */
static ssize_t blkverify_iovec_compare(QEMUIOVector *a, QEMUIOVector *b)
{
    int i;
    ssize_t offset = 0;

    assert(a->niov == b->niov);
    for (i = 0; i < a->niov; i++) {
        size_t len = 0;
        uint8_t *p = (uint8_t *)a->iov[i].iov_base;
        uint8_t *q = (uint8_t *)b->iov[i].iov_base;

        assert(a->iov[i].iov_len == b->iov[i].iov_len);
        while (len < a->iov[i].iov_len && *p++ == *q++) {
            len++;
        }

        offset += len;

        if (len != a->iov[i].iov_len) {
            return offset;
        }
    }
    return -1;
}

/**
 * Copy contents of I/O vector
 */
static void blkverify_iovec_clone(QEMUIOVector *dest, const QEMUIOVector *src,
                                  void *buf)
{
    int i;

    for (i = 0; i < src->niov; i++) {
        qemu_iovec_add(dest, buf, src->iov[i].iov_len);
        buf += src->iov[i].iov_len;
    }
}

static BlkverifyAIOCB *blkverify_aio_get(BlockDriverState *bs, bool is_write,
                                         int64_t sector_num, QEMUIOVector *qiov,
                                         int nb_sectors,
                                         BlockDriverCompletionFunc *cb,
                                         void *opaque)
{
    BlkverifyAIOCB *acb = qemu_aio_get(&blkverify_aio_pool, bs, cb, opaque);

    acb->bh = NULL;
    acb->is_write = is_write;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->ret = -EINPROGRESS;
    acb->done = 0;
    acb->qiov = qiov;
    acb->buf = NULL;
    acb->verify = NULL;
    return acb;
}

static void blkverify_aio_bh(void *opaque)
{
    BlkverifyAIOCB *acb = opaque;

    qemu_bh_delete(acb->bh);
    if (acb->buf) {
        qemu_iovec_destroy(&acb->raw_qiov);
        qemu_vfree(acb->buf);
    }
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_release(acb);
}

static void blkverify_aio_cb(void *opaque, int ret)
{
    BlkverifyAIOCB *acb = opaque;

    switch (++acb->done) {
    case 1:
        acb->ret = ret;
        break;

    case 2:
        if (acb->ret != ret) {
            blkverify_err(acb, "return value mismatch %d != %d", acb->ret, ret);
        }

        if (acb->verify) {
            acb->verify(acb);
        }

        acb->bh = qemu_bh_new(blkverify_aio_bh, acb);
        qemu_bh_schedule(acb->bh);
        break;
    }
}

static void blkverify_verify_readv(BlkverifyAIOCB *acb)
{
    ssize_t offset = blkverify_iovec_compare(acb->qiov, &acb->raw_qiov);
    if (offset != -1) {
        blkverify_err(acb, "contents mismatch in sector %ld",
                      acb->sector_num + (offset / BDRV_SECTOR_SIZE));
    }
}

static BlockDriverAIOCB *blkverify_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;
    BlkverifyAIOCB *acb = blkverify_aio_get(bs, false, sector_num, qiov,
                                            nb_sectors, cb, opaque);

    acb->verify = blkverify_verify_readv;
    acb->buf = qemu_blockalign(bs->file, qiov->size);
    qemu_iovec_init(&acb->raw_qiov, acb->qiov->niov);
    blkverify_iovec_clone(&acb->raw_qiov, qiov, acb->buf);

    if (!bdrv_aio_readv(s->test_file, sector_num, qiov, nb_sectors,
                        blkverify_aio_cb, acb)) {
        blkverify_aio_cb(acb, -EIO);
    }
    if (!bdrv_aio_readv(bs->file, sector_num, &acb->raw_qiov, nb_sectors,
                        blkverify_aio_cb, acb)) {
        blkverify_aio_cb(acb, -EIO);
    }
    return &acb->common;
}

static BlockDriverAIOCB *blkverify_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;
    BlkverifyAIOCB *acb = blkverify_aio_get(bs, true, sector_num, qiov,
                                            nb_sectors, cb, opaque);

    if (!bdrv_aio_writev(s->test_file, sector_num, qiov, nb_sectors,
                         blkverify_aio_cb, acb)) {
        blkverify_aio_cb(acb, -EIO);
    }
    if (!bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors,
                         blkverify_aio_cb, acb)) {
        blkverify_aio_cb(acb, -EIO);
    }
    return &acb->common;
}

static BlockDriverAIOCB *blkverify_aio_flush(BlockDriverState *bs,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;

    /* Only flush test file, the raw file is not important */
    return bdrv_aio_flush(s->test_file, cb, opaque);
}

static BlockDriver bdrv_blkverify = {
    .format_name        = "blkverify",
    .protocol_name      = "blkverify",

    .instance_size      = sizeof(BDRVBlkverifyState),

    .bdrv_getlength     = blkverify_getlength,

    .bdrv_file_open     = blkverify_open,
    .bdrv_close         = blkverify_close,
    .bdrv_flush         = blkverify_flush,

    .bdrv_aio_readv     = blkverify_aio_readv,
    .bdrv_aio_writev    = blkverify_aio_writev,
    .bdrv_aio_flush     = blkverify_aio_flush,
};

static void bdrv_blkverify_init(void)
{
    bdrv_register(&bdrv_blkverify);
}

block_init(bdrv_blkverify_init);
