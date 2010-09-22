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

#include "qed.h"

static int bdrv_qed_probe(const uint8_t *buf, int buf_size,
                          const char *filename)
{
    const QEDHeader *header = (const void *)buf;

    if (buf_size < sizeof(*header)) {
        return 0;
    }
    if (le32_to_cpu(header->magic) != QED_MAGIC) {
        return 0;
    }
    return 100;
}

static void qed_header_le_to_cpu(const QEDHeader *le, QEDHeader *cpu)
{
    cpu->magic = le32_to_cpu(le->magic);
    cpu->cluster_size = le32_to_cpu(le->cluster_size);
    cpu->table_size = le32_to_cpu(le->table_size);
    cpu->header_size = le32_to_cpu(le->header_size);
    cpu->features = le64_to_cpu(le->features);
    cpu->compat_features = le64_to_cpu(le->compat_features);
    cpu->l1_table_offset = le64_to_cpu(le->l1_table_offset);
    cpu->image_size = le64_to_cpu(le->image_size);
    cpu->backing_filename_offset = le32_to_cpu(le->backing_filename_offset);
    cpu->backing_filename_size = le32_to_cpu(le->backing_filename_size);
    cpu->backing_fmt_offset = le32_to_cpu(le->backing_fmt_offset);
    cpu->backing_fmt_size = le32_to_cpu(le->backing_fmt_size);
}

static void qed_header_cpu_to_le(const QEDHeader *cpu, QEDHeader *le)
{
    le->magic = cpu_to_le32(cpu->magic);
    le->cluster_size = cpu_to_le32(cpu->cluster_size);
    le->table_size = cpu_to_le32(cpu->table_size);
    le->header_size = cpu_to_le32(cpu->header_size);
    le->features = cpu_to_le64(cpu->features);
    le->compat_features = cpu_to_le64(cpu->compat_features);
    le->l1_table_offset = cpu_to_le64(cpu->l1_table_offset);
    le->image_size = cpu_to_le64(cpu->image_size);
    le->backing_filename_offset = cpu_to_le32(cpu->backing_filename_offset);
    le->backing_filename_size = cpu_to_le32(cpu->backing_filename_size);
    le->backing_fmt_offset = cpu_to_le32(cpu->backing_fmt_offset);
    le->backing_fmt_size = cpu_to_le32(cpu->backing_fmt_size);
}

static uint64_t qed_max_image_size(uint32_t cluster_size, uint32_t table_size)
{
    uint64_t table_entries;
    uint64_t l2_size;

    table_entries = (table_size * cluster_size) / sizeof(uint64_t);
    l2_size = table_entries * cluster_size;

    return l2_size * table_entries;
}

static bool qed_is_cluster_size_valid(uint32_t cluster_size)
{
    if (cluster_size < QED_MIN_CLUSTER_SIZE ||
        cluster_size > QED_MAX_CLUSTER_SIZE) {
        return false;
    }
    if (cluster_size & (cluster_size - 1)) {
        return false; /* not power of 2 */
    }
    return true;
}

static bool qed_is_table_size_valid(uint32_t table_size)
{
    if (table_size < QED_MIN_TABLE_SIZE ||
        table_size > QED_MAX_TABLE_SIZE) {
        return false;
    }
    if (table_size & (table_size - 1)) {
        return false; /* not power of 2 */
    }
    return true;
}

static bool qed_is_image_size_valid(uint64_t image_size, uint32_t cluster_size,
                                    uint32_t table_size)
{
    if (image_size == 0) {
        /* Supporting zero size images makes life harder because even the L1
         * table is not needed.  Make life simple and forbid zero size images.
         */
        return false;
    }
    if (image_size & (cluster_size - 1)) {
        return false; /* not multiple of cluster size */
    }
    if (image_size > qed_max_image_size(cluster_size, table_size)) {
        return false; /* image is too large */
    }
    return true;
}

/**
 * Read a string of known length from the image file
 *
 * @file:       Image file
 * @offset:     File offset to start of string, in bytes
 * @n:          String length in bytes
 * @buf:        Destination buffer
 * @buflen:     Destination buffer length in bytes
 *
 * The string is NUL-terminated.
 */
static int qed_read_string(BlockDriverState *file, uint64_t offset, size_t n,
                           char *buf, size_t buflen)
{
    int ret;
    if (n >= buflen) {
        return -EINVAL;
    }
    ret = bdrv_pread(file, offset, buf, n);
    if (ret != n) {
        return ret;
    }
    buf[n] = '\0';
    return 0;
}

static QEDTable *qed_alloc_table(void *opaque)
{
    BDRVQEDState *s = opaque;

    /* Honor O_DIRECT memory alignment requirements */
    return qemu_blockalign(s->bs,
                           s->header.cluster_size * s->header.table_size);
}

static int bdrv_qed_open(BlockDriverState *bs, int flags)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader le_header;
    int64_t file_size;
    int ret;

    s->bs = bs;

    ret = bdrv_pread(bs->file, 0, &le_header, sizeof(le_header));
    if (ret != sizeof(le_header)) {
        return ret;
    }
    qed_header_le_to_cpu(&le_header, &s->header);

    if (s->header.magic != QED_MAGIC) {
        return -ENOENT;
    }
    if (s->header.features & ~QED_FEATURE_MASK) {
        return -ENOTSUP; /* image uses unsupported feature bits */
    }
    if (!qed_is_cluster_size_valid(s->header.cluster_size)) {
        return -EINVAL;
    }

    /* Round up file size to the next cluster */
    file_size = bdrv_getlength(bs->file);
    if (file_size < 0) {
        return file_size;
    }
    s->file_size = qed_start_of_cluster(s, file_size);

    if (!qed_is_table_size_valid(s->header.table_size)) {
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(s->header.image_size,
                                 s->header.cluster_size,
                                 s->header.table_size)) {
        return -EINVAL;
    }
    if (!qed_check_table_offset(s, s->header.l1_table_offset)) {
        return -EINVAL;
    }

    s->table_nelems = (s->header.cluster_size * s->header.table_size) /
                      sizeof(uint64_t);
    s->l2_shift = get_bits_from_size(s->header.cluster_size);
    s->l2_mask = s->table_nelems - 1;
    s->l1_shift = s->l2_shift + get_bits_from_size(s->l2_mask + 1);

    if ((s->header.features & QED_F_BACKING_FILE)) {
        ret = qed_read_string(bs->file, s->header.backing_filename_offset,
                              s->header.backing_filename_size, bs->backing_file,
                              sizeof(bs->backing_file));
        if (ret < 0) {
            return ret;
        }

        if ((s->header.compat_features & QED_CF_BACKING_FORMAT)) {
            ret = qed_read_string(bs->file, s->header.backing_fmt_offset,
                                  s->header.backing_fmt_size,
                                  bs->backing_format,
                                  sizeof(bs->backing_format));
            if (ret < 0) {
                return ret;
            }
        }
    }

    s->l1_table = qed_alloc_table(s);
    qed_init_l2_cache(&s->l2_cache, qed_alloc_table, s);

    ret = qed_read_l1_table_sync(s);
    if (ret) {
        qed_free_l2_cache(&s->l2_cache);
        qemu_vfree(s->l1_table);
    }
    return ret;
}

static void bdrv_qed_close(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    qed_free_l2_cache(&s->l2_cache);
    qemu_vfree(s->l1_table);
}

static void bdrv_qed_flush(BlockDriverState *bs)
{
    bdrv_flush(bs->file);
}

static int qed_create(const char *filename, uint32_t cluster_size,
                      uint64_t image_size, uint32_t table_size,
                      const char *backing_file, const char *backing_fmt)
{
    QEDHeader header = {
        .magic = QED_MAGIC,
        .cluster_size = cluster_size,
        .table_size = table_size,
        .header_size = 1,
        .features = 0,
        .compat_features = 0,
        .l1_table_offset = cluster_size,
        .image_size = image_size,
    };
    QEDHeader le_header;
    uint8_t *l1_table = NULL;
    size_t l1_size = header.cluster_size * header.table_size;
    int ret = 0;
    int fd;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        return -errno;
    }

    if (backing_file) {
        header.features |= QED_F_BACKING_FILE;
        header.backing_filename_offset = sizeof(le_header);
        header.backing_filename_size = strlen(backing_file);
        if (backing_fmt) {
            header.compat_features |= QED_CF_BACKING_FORMAT;
            header.backing_fmt_offset = header.backing_filename_offset +
                                        header.backing_filename_size;
            header.backing_fmt_size = strlen(backing_fmt);
        }
    }

    qed_header_cpu_to_le(&header, &le_header);
    if (qemu_write_full(fd, &le_header, sizeof(le_header)) != sizeof(le_header)) {
        ret = -errno;
        goto out;
    }
    if (qemu_write_full(fd, backing_file, header.backing_filename_size) != header.backing_filename_size) {
        ret = -errno;
        goto out;
    }
    if (qemu_write_full(fd, backing_fmt, header.backing_fmt_size) != header.backing_fmt_size) {
        ret = -errno;
        goto out;
    }

    l1_table = qemu_mallocz(l1_size);
    lseek(fd, header.l1_table_offset, SEEK_SET);
    if (qemu_write_full(fd, l1_table, l1_size) != l1_size) {
        ret = -errno;
        goto out;
    }

out:
    qemu_free(l1_table);
    close(fd);
    return ret;
}

static int bdrv_qed_create(const char *filename, QEMUOptionParameter *options)
{
    uint64_t image_size = 0;
    uint32_t cluster_size = QED_DEFAULT_CLUSTER_SIZE;
    uint32_t table_size = QED_DEFAULT_TABLE_SIZE;
    const char *backing_file = NULL;
    const char *backing_fmt = NULL;

    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            image_size = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FMT)) {
            backing_fmt = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                cluster_size = options->value.n;
            }
        } else if (!strcmp(options->name, "table_size")) {
            if (options->value.n) {
                table_size = options->value.n;
            }
        }
        options++;
    }

    if (!qed_is_cluster_size_valid(cluster_size)) {
        fprintf(stderr, "QED cluster size must be within range [%u, %u] and power of 2\n",
                QED_MIN_CLUSTER_SIZE, QED_MAX_CLUSTER_SIZE);
        return -EINVAL;
    }
    if (!qed_is_table_size_valid(table_size)) {
        fprintf(stderr, "QED table size must be within range [%u, %u] and power of 2\n",
                QED_MIN_TABLE_SIZE, QED_MAX_TABLE_SIZE);
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(image_size, cluster_size, table_size)) {
        char buffer[64];

        bytes_to_str(buffer, sizeof(buffer),
                     qed_max_image_size(cluster_size, table_size));

        fprintf(stderr,
                "QED image size must be a non-zero multiple of cluster size and less than %s\n",
                buffer);
        return -EINVAL;
    }

    return qed_create(filename, cluster_size, image_size, table_size,
                      backing_file, backing_fmt);
}

typedef struct {
    int is_allocated;
    int *pnum;
} QEDIsAllocatedCB;

static void qed_is_allocated_cb(void *opaque, int ret, uint64_t offset, size_t len)
{
    QEDIsAllocatedCB *cb = opaque;
    *cb->pnum = len / BDRV_SECTOR_SIZE;
    cb->is_allocated = ret == QED_CLUSTER_FOUND;
}

static int bdrv_qed_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                  int nb_sectors, int *pnum)
{
    BDRVQEDState *s = bs->opaque;
    uint64_t pos = (uint64_t)sector_num * BDRV_SECTOR_SIZE;
    size_t len = (size_t)nb_sectors * BDRV_SECTOR_SIZE;
    QEDIsAllocatedCB cb = {
        .is_allocated = -1,
        .pnum = pnum,
    };
    QEDRequest request = { .l2_table = NULL };

    async_context_push();

    qed_find_cluster(s, &request, pos, len, qed_is_allocated_cb, &cb);

    while (cb.is_allocated == -1) {
        qemu_aio_wait();
    }

    async_context_pop();

    qed_unref_l2_cache_entry(&s->l2_cache, request.l2_table);

    return cb.is_allocated;
}

static int bdrv_qed_make_empty(BlockDriverState *bs)
{
    return -ENOTSUP;
}

static BlockDriverAIOCB *bdrv_qed_aio_readv(BlockDriverState *bs,
                                            int64_t sector_num,
                                            QEMUIOVector *qiov, int nb_sectors,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque)
{
    return NULL;
}

static BlockDriverAIOCB *bdrv_qed_aio_writev(BlockDriverState *bs,
                                             int64_t sector_num,
                                             QEMUIOVector *qiov, int nb_sectors,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    return NULL;
}

static BlockDriverAIOCB *bdrv_qed_aio_flush(BlockDriverState *bs,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque)
{
    return bdrv_aio_flush(bs->file, cb, opaque);
}

static int bdrv_qed_truncate(BlockDriverState *bs, int64_t offset)
{
    return -ENOTSUP;
}

static int64_t bdrv_qed_getlength(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;
    return s->header.image_size;
}

static int bdrv_qed_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQEDState *s = bs->opaque;

    memset(bdi, 0, sizeof(*bdi));
    bdi->cluster_size = s->header.cluster_size;
    return 0;
}

static int bdrv_qed_change_backing_file(BlockDriverState *bs,
                                        const char *backing_file,
                                        const char *backing_fmt)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader new_header, le_header;
    void *buffer;
    size_t buffer_len, backing_file_len, backing_fmt_len;
    int ret;

    /* Refuse to set backing filename if unknown compat feature bits are
     * active.  If the image uses an unknown compat feature then we may not
     * know the layout of data following the header structure and cannot safely
     * add a new string.
     */
    if (backing_file && (s->header.compat_features &
                         ~QED_COMPAT_FEATURE_MASK)) {
        return -ENOTSUP;
    }

    memcpy(&new_header, &s->header, sizeof(new_header));

    new_header.features &= ~QED_F_BACKING_FILE;
    new_header.compat_features &= ~QED_CF_BACKING_FORMAT;

    /* Adjust feature flags */
    if (backing_file) {
        new_header.features |= QED_F_BACKING_FILE;
        if (backing_fmt) {
            new_header.compat_features |= QED_CF_BACKING_FORMAT;
        }
    }

    /* Calculate new header size */
    backing_file_len = backing_fmt_len = 0;

    if (backing_file) {
        backing_file_len = strlen(backing_file);
        if (backing_fmt) {
            backing_fmt_len = strlen(backing_fmt);
        }
    }

    buffer_len = sizeof(new_header);
    new_header.backing_filename_offset = buffer_len;
    new_header.backing_filename_size = backing_file_len;
    buffer_len += backing_file_len;
    new_header.backing_fmt_offset = buffer_len;
    new_header.backing_fmt_size = backing_fmt_len;
    buffer_len += backing_fmt_len;

    /* Make sure we can rewrite header without failing */
    if (buffer_len > new_header.header_size * new_header.cluster_size) {
        return -ENOSPC;
    }

    /* Prepare new header */
    buffer = qemu_malloc(buffer_len);

    qed_header_cpu_to_le(&new_header, &le_header);
    memcpy(buffer, &le_header, sizeof(le_header));
    buffer_len = sizeof(le_header);

    memcpy(buffer + buffer_len, backing_file, backing_file_len);
    buffer_len += backing_file_len;

    memcpy(buffer + buffer_len, backing_fmt, backing_fmt_len);
    buffer_len += backing_fmt_len;

    /* Write new header */
    ret = bdrv_pwrite_sync(bs->file, 0, buffer, buffer_len);
    qemu_free(buffer);
    if (ret == 0) {
        memcpy(&s->header, &new_header, sizeof(new_header));
    }
    return ret;
}

static int bdrv_qed_check(BlockDriverState *bs, BdrvCheckResult *result)
{
    return -ENOTSUP;
}

static QEMUOptionParameter qed_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size (in bytes)"
    }, {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    }, {
        .name = BLOCK_OPT_BACKING_FMT,
        .type = OPT_STRING,
        .help = "Image format of the base image"
    }, {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "Cluster size (in bytes)"
    }, {
        .name = "table_size",
        .type = OPT_SIZE,
        .help = "L1/L2 table size (in clusters)"
    },
    { /* end of list */ }
};

static BlockDriver bdrv_qed = {
    .format_name = "qed",
    .instance_size = sizeof(BDRVQEDState),
    .create_options = qed_create_options,

    .bdrv_probe = bdrv_qed_probe,
    .bdrv_open = bdrv_qed_open,
    .bdrv_close = bdrv_qed_close,
    .bdrv_create = bdrv_qed_create,
    .bdrv_flush = bdrv_qed_flush,
    .bdrv_is_allocated = bdrv_qed_is_allocated,
    .bdrv_make_empty = bdrv_qed_make_empty,
    .bdrv_aio_readv = bdrv_qed_aio_readv,
    .bdrv_aio_writev = bdrv_qed_aio_writev,
    .bdrv_aio_flush = bdrv_qed_aio_flush,
    .bdrv_truncate = bdrv_qed_truncate,
    .bdrv_getlength = bdrv_qed_getlength,
    .bdrv_get_info = bdrv_qed_get_info,
    .bdrv_change_backing_file = bdrv_qed_change_backing_file,
    .bdrv_check = bdrv_qed_check,
};

static void bdrv_qed_init(void)
{
    bdrv_register(&bdrv_qed);
}

block_init(bdrv_qed_init);
