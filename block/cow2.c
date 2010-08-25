#include "block_int.h"

typedef struct {
    BlockDriverState *hd;
} BDRVCow2State;

static int bdrv_cow2_probe(const uint8_t *buf, int buf_size,
                           const char *filename)
{
    return 0; /* TODO */
}

static int bdrv_cow2_open(BlockDriverState *bs, int flags)
{
    return -ENOTSUP; /* TODO */
}

static void bdrv_cow2_close(BlockDriverState *bs)
{
    /* TODO */
}

static void bdrv_cow2_flush(BlockDriverState *bs)
{
    /* TODO */
}

static int bdrv_cow2_create(const char *filename, QEMUOptionParameter *options)
{
    return -ENOTSUP; /* TODO */
}

static int bdrv_cow2_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                  int nb_sectors, int *pnum)
{
    return 0; /* TODO */
}

static int bdrv_cow2_make_empty(BlockDriverState *bs)
{
    return -ENOTSUP; /* TODO */
}

static BlockDriverAIOCB *bdrv_cow2_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return NULL; /* TODO */
}

static BlockDriverAIOCB *bdrv_cow2_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return NULL; /* TODO */
}

static BlockDriverAIOCB *bdrv_cow2_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return NULL; /* TODO */
}

static int bdrv_cow2_truncate(BlockDriverState *bs, int64_t offset)
{
    return -ENOTSUP; /* TODO */
}

static int64_t bdrv_cow2_getlength(BlockDriverState *bs)
{
    return -ENOTSUP; /* TODO */
}

static int bdrv_cow2_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return -ENOTSUP; /* TODO */
}

static int bdrv_cow2_change_backing_file(BlockDriverState *bs,
        const char *backing_file, const char *backing_fmt)
{
    return -ENOTSUP; /* TODO */
}

static int bdrv_cow2_check(BlockDriverState* bs, BdrvCheckResult *result)
{
    return -ENOTSUP; /* TODO */
}

static QEMUOptionParameter cow2_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size (in bytes)"
    },
    {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    },
    {
        .name = BLOCK_OPT_BACKING_FMT,
        .type = OPT_STRING,
        .help = "Image format of the base image"
    },
    {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "Cluster size (in bytes)"
    },
    {
        .name = BLOCK_OPT_PREALLOC,
        .type = OPT_STRING,
        .help = "Preallocation mode (allowed values: off, metadata)"
    },
    { NULL }
};

static BlockDriver bdrv_cow2 = {
    .format_name = "cow2",
    .instance_size = sizeof(BDRVCow2State),
    .create_options = cow2_create_options,

    .bdrv_probe = bdrv_cow2_probe,
    .bdrv_open = bdrv_cow2_open,
    .bdrv_close = bdrv_cow2_close,
    .bdrv_create = bdrv_cow2_create,
    .bdrv_flush = bdrv_cow2_flush,
    .bdrv_is_allocated = bdrv_cow2_is_allocated,
    .bdrv_make_empty = bdrv_cow2_make_empty,
    .bdrv_aio_readv = bdrv_cow2_aio_readv,
    .bdrv_aio_writev = bdrv_cow2_aio_writev,
    .bdrv_aio_flush = bdrv_cow2_aio_flush,
    .bdrv_truncate = bdrv_cow2_truncate,
    .bdrv_getlength = bdrv_cow2_getlength,
    .bdrv_get_info = bdrv_cow2_get_info,
    .bdrv_change_backing_file = bdrv_cow2_change_backing_file,
    .bdrv_check = bdrv_cow2_check,
};

static void bdrv_cow2_init(void)
{
    bdrv_register(&bdrv_cow2);
}

block_init(bdrv_cow2_init);
