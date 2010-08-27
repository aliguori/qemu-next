#include "trace.h"
#include "block_int.h"

/* TODO blkdebug support */
/* TODO avoid corruption caused by interference between pending requests.
 * Asynchronous tasks are interleaved across their blocking points.  This means
 * any global resource can be mutated across a blocking point.  One solution is
 * to introduce waitqueues for L2 and data cluster operations as well as L2
 * cache operations.
 */

enum {
    COW2_MAGIC = 'C' | 'O' << 8 | 'W' << 16 | '2' << 24,

    /* Feature bits must be used when the on-disk format changes */
    COW2_FEATURE_MASK = 0x0,        /* supported feature bits */

    /* Data is stored in groups of sectors called clusters.  Cluster size must
     * be large to avoid keeping too much metadata.  I/O requests that have
     * sub-cluster size will require read-modify-write.
     */
    COW2_MIN_CLUSTER_SIZE = 4 * 1024, /* in bytes */
    COW2_MAX_CLUSTER_SIZE = 64 * 1024 * 1024,
    COW2_DEFAULT_CLUSTER_SIZE = 64 * 1024,

    /* Allocated clusters are tracked using a 2-level pagetable.  Table size is
     * a multiple of clusters so large maximum image sizes can be supported
     * without jacking up the cluster size too much.
     */
    COW2_MIN_TABLE_SIZE = 1,        /* in clusters */
    COW2_MAX_TABLE_SIZE = 16,
    COW2_DEFAULT_TABLE_SIZE = 4,
};

/* The layout of a COW2 file is as follows:
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
    uint32_t magic;                 /* COW2 */
    uint32_t features;              /* format feature bits */

    uint32_t cluster_size;          /* in bytes */
    uint32_t table_size;            /* table size, in clusters */
    uint64_t l1_table_offset;       /* L1 table offset, in bytes */
    uint64_t image_size;            /* total image size, in bytes */

    uint32_t backing_file_offset;   /* in bytes from start of header */
    uint32_t backing_file_size;     /* in bytes */
    uint32_t backing_fmt_offset;    /* in bytes from start of header */
    uint32_t backing_fmt_size;      /* in bytes */
} Cow2Header;

typedef struct {
    uint64_t offsets[0];            /* in bytes */
} Cow2Table;

typedef struct {
    BlockDriverState *file;         /* image file */
    uint64_t file_size;             /* length of image file, in bytes */

    Cow2Header header;              /* always cpu-endian */
    Cow2Table *l1_table;
    Cow2Table *l2_table;            /* cached L2 table */
    uint64_t l2_table_offset;
} BDRVCow2State;

typedef struct {
    BlockDriverAIOCB common;
    QEMUIOVector *qiov;

    uint64_t cur_pos;               /* position on block device, in bytes */
    uint64_t end_pos;
    uint64_t cur_offset;            /* cluster offset in image file */
    QEMUIOVector cur_qiov;
} Cow2AIOCB;

static void cow2_aio_cancel(BlockDriverAIOCB *acb)
{
    qemu_aio_release(acb);
}

static AIOPool cow2_aio_pool = {
    .aiocb_size         = sizeof(Cow2AIOCB),
    .cancel             = cow2_aio_cancel,
};

static int bdrv_cow2_probe(const uint8_t *buf, int buf_size,
                           const char *filename)
{
    const Cow2Header *header = (const void *)buf;

    if (buf_size < sizeof *header) {
        return 0;
    }
    if (le32_to_cpu(header->magic) != COW2_MAGIC) {
        return 0;
    }
    return 100;
}

static void cow2_header_le_to_cpu(const Cow2Header *le, Cow2Header *cpu)
{
    cpu->magic = le32_to_cpu(le->magic);
    cpu->features = le32_to_cpu(le->features);
    cpu->cluster_size = le32_to_cpu(le->cluster_size);
    cpu->table_size = le32_to_cpu(le->table_size);
    cpu->l1_table_offset = le64_to_cpu(le->l1_table_offset);
    cpu->image_size = le64_to_cpu(le->image_size);
    cpu->backing_file_offset = le32_to_cpu(le->backing_file_offset);
    cpu->backing_file_size = le32_to_cpu(le->backing_file_size);
    cpu->backing_fmt_offset = le32_to_cpu(le->backing_fmt_offset);
    cpu->backing_fmt_size = le32_to_cpu(le->backing_fmt_size);
}

static void cow2_header_cpu_to_le(const Cow2Header *cpu, Cow2Header *le)
{
    le->magic = cpu_to_le32(cpu->magic);
    le->features = cpu_to_le32(cpu->features);
    le->cluster_size = cpu_to_le32(cpu->cluster_size);
    le->table_size = cpu_to_le32(cpu->table_size);
    le->l1_table_offset = cpu_to_le64(cpu->l1_table_offset);
    le->image_size = cpu_to_le64(cpu->image_size);
    le->backing_file_offset = cpu_to_le32(cpu->backing_file_offset);
    le->backing_file_size = cpu_to_le32(cpu->backing_file_size);
    le->backing_fmt_offset = cpu_to_le32(cpu->backing_fmt_offset);
    le->backing_fmt_size = cpu_to_le32(cpu->backing_fmt_size);
}

static bool cow2_is_cluster_size_valid(uint32_t cluster_size)
{
    if (cluster_size < COW2_MIN_CLUSTER_SIZE ||
        cluster_size > COW2_MAX_CLUSTER_SIZE) {
        return false;
    }
    if (cluster_size & (cluster_size - 1)) {
        return false; /* not power of 2 */
    }
    return true;
}

static bool cow2_is_image_size_valid(uint64_t image_size, uint32_t cluster_size)
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
    return true;
}

/**
 * Test if a byte offset is cluster aligned and within the image file
 */
static bool cow2_check_byte_offset(BDRVCow2State *s, uint64_t offset) {
    if (offset & (s->header.cluster_size - 1)) {
        return false;
    }
    if (offset == 0) {
        return false; /* first cluster contains the header and is not valid */
    }
    return offset < s->file_size;
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
static int cow2_read_string(BlockDriverState *file, uint64_t offset, size_t n,
                            char *buf, size_t buflen)
{
    int rc;
    if (n >= buflen) {
        return -EINVAL;
    }
    rc = bdrv_pread(file, offset, buf, n);
    if (rc != n) {
        return rc;
    }
    buf[n] = '\0';
    return 0;
}

typedef struct {
    BDRVCow2State *s;
    Cow2Table *table;

    struct iovec iov;
    QEMUIOVector qiov;

    /* User callback */
    BlockDriverCompletionFunc *cb;
    void *opaque;
} Cow2ReadTableCB;

static void cow2_read_table_cb(void *opaque, int ret)
{
    Cow2ReadTableCB *read_table_cb = opaque;
    Cow2Table *table = read_table_cb->table;
    int noffsets = read_table_cb->iov.iov_len / sizeof(uint64_t);
    int i;

    /* Handle I/O error */
    if (ret) {
        goto out;
    }

    /* Byteswap and verify offsets */
    for (i = 0; i < noffsets; i++) {
        uint64_t offset = le64_to_cpu(table->offsets[i]);
        if (offset && !cow2_check_byte_offset(read_table_cb->s, offset)) {
            /* If the offset is invalid then the image is broken.  If there was
             * a power failure and the table update reached storage but the
             * data being pointed to did not, forget about the lost data by
             * clearing the offset.
             */
            offset = 0;
        }
        table->offsets[i] = offset;
    }

out:
    /* Completion */
    trace_cow2_read_table_cb(read_table_cb->s, read_table_cb->table, ret);
    read_table_cb->cb(read_table_cb->opaque, ret);
    qemu_free(read_table_cb);
}

static void cow2_read_table(BDRVCow2State *s, uint64_t offset, Cow2Table *table,
                            BlockDriverCompletionFunc *cb, void *opaque)
{
    Cow2ReadTableCB *read_table_cb = qemu_malloc(sizeof *read_table_cb);
    QEMUIOVector *qiov = &read_table_cb->qiov;
    BlockDriverAIOCB *aiocb;

    trace_cow2_read_table(s, offset, table);

    read_table_cb->s = s;
    read_table_cb->table = table;
    read_table_cb->iov.iov_base = table->offsets,
    read_table_cb->iov.iov_len = s->header.cluster_size * s->header.table_size,
    read_table_cb->cb = cb;
    read_table_cb->opaque = opaque;

    qemu_iovec_init_external(qiov, &read_table_cb->iov, 1);
    aiocb = bdrv_aio_readv(s->file, offset / BDRV_SECTOR_SIZE, qiov,
                           read_table_cb->iov.iov_len * BDRV_SECTOR_SIZE,
                           cow2_read_table_cb, read_table_cb);
    if (!aiocb) {
        cow_read_table_cb(read_table_cb, -EIO);
    }
}

typedef struct {
    BDRVCow2State *s;               /* these are mainly for tracing */
    Cow2Table *orig_table;

    struct iovec iov;
    QEMUIOVector qiov;

    /* User callback */
    BlockDriverCompletionFunc *cb;
    void *opaque;

    Cow2Table table;
} Cow2WriteTableCB;

static void cow2_write_table_cb(void *opaque, int ret)
{
    Cow2WriteTableCB *write_table_cb = opaque;

    trace_cow2_write_table_cb(write_table_cb->s,
                              write_table_cb->orig_table, ret);

    write_table_cb->cb(write_table_cb->opaque, ret);
    qemu_free(write_table_cb);
}

static void cow2_write_table(BDRVCow2State *s, uint64_t offset,
                             Cow2Table *table, BlockDriverCompletionFunc *cb,
                             void *opaque)
{
    size_t table_size = s->header.cluster_size * s->header.table_size;
    int noffsets = table_size / sizeof(uint64_t);
    Cow2WriteTableCB *write_table_cb = qemu_malloc(sizeof *write_table_cb +
                                                   table_size);
    Cow2Table *le_table = &write_table_cb->table;
    QEMUIOVector *qiov = &write_table_cb->qiov;

    trace_cow2_write_table(s, offset, table);

    write_table_cb->s = s;
    write_table_cb->orig_table = table;
    write_table_cb->iov.iov_base = write_table_cb->table.offsets;
    write_table_cb->iov.iov_len = table_size;

    /* Byteswap table */
    for (i = 0; i < noffsets; i++) {
        le_table->offsets[i] = cpu_to_le64(table->offsets[i]);
    }

    qemu_iovec_init_external(qiov, &write_table_cb->iov, 1);
    aiocb = bdrv_aio_writev(s->file, offset / BDRV_SECTOR_SIZE, qiov,
                            write_stable_cb->iov.iov_len * BDRV_SECTOR_SIZE,
                            cow2_write_table_cb, write_table_cb);
    if (!aiocb) {
        cow_write_table_cb(write_table_cb, -EIO);
    }
}

typedef struct {
    BDRVCow2State *s;
    uint64_t l2_offset;

    /* User callback */
    BlockDriverCompletionFunc *cb;
    void *opaque;
} Cow2ReadL2TableCB;

static void cow2_read_l2_table_cb(void *opaque, int ret)
{
    Cow2ReadL2TableCB *read_l2_table_cb = opaque;

    if (ret) {
        s->l2_table_offset = 0; /* can't trust loaded L2 table anymore */
    } else {
        s->l2_table_offset = read_l2_table_cb->l2_offset;
    }

    read_l2_table_cb->cb(read_l2_table_cb->opaque, ret);
    qemu_free(read_l2_table_cb); /* TODO convert all these into tail calls, move free before cb? */
}

static void cow2_read_l2_table(BDRVCow2State *s, uint64_t offset,
                               BlockDriverCompletionFunc *cb, void *opaque)
{
    Cow2ReadL2TableCB *read_l2_table_cb;
    int ret;

    /* Check for cached L2 table */
    if (s->l2_table_offset == offset) {
        cb(opaque, 0);
        return;
    }

    if (!s->l2_table) {
        s->l2_table = qemu_malloc(s->header.chunk_size * s->header.table_size);
    }

    read_l2_table_cb = qemu_malloc(sizeof *read_l2_table_cb);
    read_l2_table_cb->s = s;
    read_l2_table_cb->l2_offset = offset;
    read_l2_table_cb->cb = cb;
    read_l2_table_cb->opaque = opaque;

    cow2_read_table(s, offset, s->l2_table, cow2_read_l2_table, read_l2_table_cb);
}

static void cow2_write_l2_table(BDRVCow2State *s,
                                BlockDriverCompletionFunc *cb, void *opaque)
{
    cow2_write_table(s, s->l2_table_offset, s->l2_table, cb, opaque);
}

static void cow2_read_l1_table_cb(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

/**
 * Read the L1 table synchronously
 */
static int cow2_read_l1_table(BDRVCow2State *s)
{
    int ret;

    s->l1_table = qemu_malloc(s->header.cluster_size * s->header.table_size);

    /* TODO push/pop async context? */

    ret = -EINPROGRESS;
    cow2_read_table(s, s->header.l1_table_offset, s->l1_table, cow2_read_l1_table_cb, &ret);
    while (ret == -EINPROGRESS) {
        qemu_aio_wait();
    }

out:
    if (ret) {
        qemu_free(s->l1_table);
        s->l1_table = NULL;
    }
    return ret;
}

static int bdrv_cow2_open(BlockDriverState *bs, int flags)
{
    BDRVCow2State *s = bs->opaque;
    Cow2Header le_header;
    int64_t file_size;
    int rc;

    s->file = bs->file;

    file_size = bdrv_getlength(s->file);
    if (file_size < 0) {
        return file_size;
    }
    s->file_size = file_size;

    rc = bdrv_pread(s->file, 0, &le_header, sizeof le_header);
    if (rc != sizeof le_header) {
        return rc;
    }
    cow2_header_le_to_cpu(&le_header, &s->header);

    if (s->header.magic != COW2_MAGIC) {
        return -ENOENT;
    }
    if (s->header.features & ~COW2_FEATURE_MASK) {
        return -ENOTSUP; /* image uses unsupported feature bits */
    }
    if (!cow2_is_cluster_size_valid(s->header.cluster_size)) {
        return -EINVAL;
    }
    if (s->file_size & (s->header.cluster_size - 1)) {
        return -EINVAL;
    }
    if (s->header.table_size < COW2_MIN_TABLE_SIZE ||
        s->header.table_size > COW2_MAX_TABLE_SIZE) {
        return -EINVAL;
    }
    if (!cow2_is_image_size_valid(s->header.image_size,
                                  s->header.cluster_size)) {
        return -EINVAL;
    }
    if (!cow2_check_byte_offset(s, s->header.l1_table_offset)) {
        return -EINVAL;
    }

    if (s->header.backing_file_offset) {
        /* Must have backing format */
        if (!s->header.backing_fmt_offset) {
            return -EINVAL;
        }

        rc = cow2_read_string(s->file, s->header.backing_file_offset,
                              s->header.backing_file_size, bs->backing_file,
                              sizeof bs->backing_file);
        if (rc < 0) {
            return rc;
        }

        rc = cow2_read_string(s->file, s->header.backing_fmt_offset,
                              s->header.backing_fmt_size, bs->backing_format,
                              sizeof bs->backing_format);
        if (rc < 0) {
            return rc;
        }
    }

    return cow2_read_l1_table(s);
}

static void bdrv_cow2_close(BlockDriverState *bs)
{
    BDRVCow2State *s = bs->opaque;

    qemu_free(s->l2_table);
    qemu_free(s->l1_table);
}

static void bdrv_cow2_flush(BlockDriverState *bs)
{
    bdrv_flush(bs->file);
}

static int cow2_create(const char *filename, uint32_t cluster_size,
                       uint64_t image_size, const char *backing_file,
                       const char *backing_fmt)
{
    Cow2Header header = {
        .magic = COW2_MAGIC,
        .features = 0,
        .cluster_size = cluster_size,
        .table_size = COW2_DEFAULT_TABLE_SIZE,
        .l1_table_offset = cluster_size,
        .image_size = image_size,
    };
    Cow2Header le_header;
    uint8_t *l1_table = NULL;
    size_t l1_size = header.cluster_size * header.table_size;
    int rc = 0;
    int fd;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        return -errno;
    }

    if (backing_file && backing_fmt) {
        header.backing_file_offset = sizeof le_header;
        header.backing_file_size = strlen(backing_file);
        header.backing_fmt_offset = header.backing_file_offset +
                                    header.backing_file_size;
        header.backing_fmt_size = strlen(backing_fmt);
    }

    cow2_header_cpu_to_le(&header, &le_header);
    if (qemu_write_full(fd, &le_header, sizeof le_header) != sizeof le_header) {
        rc = -errno;
        goto out;
    }
    if (qemu_write_full(fd, backing_file, header.backing_file_size) != header.backing_file_size) {
        rc = -errno;
        goto out;
    }
    if (qemu_write_full(fd, backing_fmt, header.backing_fmt_size) != header.backing_fmt_size) {
        rc = -errno;
        goto out;
    }

    l1_table = qemu_mallocz(l1_size);
    lseek(fd, header.l1_table_offset, SEEK_SET);
    if (qemu_write_full(fd, l1_table, l1_size) != l1_size) {
        rc = -errno;
        goto out;
    }

out:
    qemu_free(l1_table);
    close(fd);
    return rc;
}

static int bdrv_cow2_create(const char *filename, QEMUOptionParameter *options)
{
    uint64_t image_size = 0;
    uint32_t cluster_size = COW2_DEFAULT_CLUSTER_SIZE;
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
        }
        options++;
    }

    if (!cow2_is_cluster_size_valid(cluster_size)) {
        fprintf(stderr, "Cow2 cluster size must be within range [%u, %u] and power of 2\n",
                COW2_MIN_CLUSTER_SIZE, COW2_MAX_CLUSTER_SIZE);
        return -EINVAL;
    }
    if (!cow2_is_image_size_valid(image_size, cluster_size)) {
        fprintf(stderr, "Cow2 image size must be a non-zero multiple of cluster size\n");
        return -EINVAL;
    }
    if (backing_file && !backing_fmt) {
        fprintf(stderr, "Cow2 requires a backing file format when a backing file is specified\n");
        return -EINVAL;
    }

    return cow2_create(filename, cluster_size, image_size,
                       backing_file, backing_fmt);
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

static BDRVCow2State *acb_to_s(Cow2AIOCB *acb)
{
    return acb->common.bs->opaque;
}

static void cow2_aio_complete(Cow2AIOCB *acb, int ret)
{
    trace_cow2_aio_complete(acb_to_s(acb), acb, ret);
    acb->common.cb(acb->common.opaque, ret);
    qemu_aio_release(acb);
}

/**
 * Allocate new clusters
 *
 * @s:          COW2 state
 * @n:          Number of contiguous clusters to allocate
 * @offset:     Offset of first allocated cluster, filled in on success
 */
static int cow2_alloc_clusters(BDRVCow2State *s, unsigned int n, uint64_t *offset)
{
    *offset = s->file_size;
    s->file_size += n * s->header.cluster_size;
    return 0;
}

enum {
    COW2_CLUSTER_FOUND,         /* cluster found */
    COW2_CLUSTER_L2,            /* cluster missing in L2 */
    COW2_CLUSTER_L1,            /* cluster missing in L1 */
    COW2_CLUSTER_ERROR,         /* error looking up cluster */
};

typedef void Cow2FindClusterFunc(void *opaque, int ret, uint64_t offset);
typedef struct {
    uint64_t cluster;

    /* User callback */
    Cow2FindClusterFunc *cb;
    void *opaque;
} Cow2FindClusterCB;

static void cow2_find_cluster_cb(void *opaque, int ret)
{
    Cow2FindClusterCB *find_cluster_cb = opaque;
    uint64_t offset = 0;

    if (ret) {
        ret = COW2_CLUSTER_ERR;
        goto out;
    }

    offset = s->l2_table[(cluster >> s->l2_shift) & s->l2_mask];
    ret = offset ? COW2_CLUSTER_FOUND : COW2_CLUSTER_L2;

out:
    find_cluster_cb->cb(find_cluster_cb->opaque, ret, offset);
    qemu_free(find_cluster_cb);
}

/**
 * Find the offset of a data cluster
 *
 * @cluster:    Cluster position in bytes
 * @cb:         Completion function
 * @opaque:     User data for completion function
 */
static void cow2_find_cluster(BDRVCow2State *s, uint64_t cluster,
                              Cow2FindClusterFunc *cb, void *opaque)
{
    Cow2FindClusterCB *find_cluster_cb;
    uint64_t l2_offset = s->l1_table[cluster >> s->l1_shift];
    if (!l2_offset) {
        cb(opaque, COW2_CLUSTER_L1, 0);
        return;
    }

    find_cluster_cb = qemu_malloc(sizeof *find_cluster_cb);
    find_cluster_cb->cluster = cluster;
    find_cluster_cb->cb = cb;
    find_cluster_cb->opaque = opaque;

    cow2_read_l2_table(s, l2_offset, cow2_find_cluster_cb, find_cluster_cb);
}

/**
 * Construct an iovec array for the current cluster
 *
 * @acb:        I/O request
 * @zero:       true to zero regions not touched by the request
 */
static void cow2_acb_build_qiov(Cow2AIOCB *acb, bool zero)
{
    /* TODO */
    fprintf(stderr, "%s not implemented\n", __func__);
    exit(1);
}

/**
 * Update L2 table with new cluster offset and write it out
 */
static void cow2_aio_write_l2_update(void *opaque, int ret)
{
    Cow2AIOCB *acb = opaque;
    BDRVCow2State *s = acb_to_s(acb);

    if (ret) {
        cow2_aio_write_next_cluster(acb, ret);
        return;
    }

    /* TODO hold reference to l2_table */
    s->l2_table[(s->cur_pos >> s->l2_shift) & s->l2_mask] = s->cur_offset & ~(uint64_t)s->header.cluster_size;

    cow2_write_l2_table(s, cow2_aio_write_next_cluster, acb);
}

/**
 * Update L1 table with new L2 table offset and write it out
 */
static void cow2_aio_write_l1_update(void *opaque, int ret)
{
}

/**
 * Allocate new L2 table and update L1 table
 */
static void cow2_aio_write_l2_alloc(void *opaque, int ret)
{
    Cow2AIOCB *acb = opaque;
    BDRVCow2State *s = acb_to_s(acb);

    if (ret) {
        goto err;
    }

    ret = cow2_new_l2_table(s);
    if (ret) {
        goto err;
    }

    /* TODO hold reference to l2_table */
    s->l2_table[(s->cur_pos >> s->l2_shift) & s->l2_mask] = s->cur_offset & ~(uint64_t)s->header.cluster_size;

    cow2_write_l2_table(s, cow2_aio_write_l1_update, acb);
    return;

err:
    cow2_aio_write_next_cluster(acb, ret);
}

/**

/**
 * Write data cluster
 *
 * @opaque:     Write request
 * @ret:        COW2_CLUSTER_FOUND, COW2_CLUSTER_L2, COW2_CLUSTER_L1,
 *              or COW2_CLUSTER_ERROR
 * @offset:     Cluster offset in bytes
 *
 * Callback from cow2_find_cluster().
 */
static void cow2_aio_write_data(void *opaque, int ret, uint64_t offset)
{
    /* After writing out the data cluster it is necessary to update or allocate
     * new metadata if allocation has taken place.
     */
    static const BlockDriverCompletionFunc *cbs[] = {
        [COW2_CLUSTER_FOUND] = cow2_aio_write_next_cluster,
        [COW2_CLUSTER_L2] = cow2_aio_write_l2_update,
        [COW2_CLUSTER_L1] = cow2_aio_write_l2_alloc,
    };

    Cow2AIOCB *acb = opaque;
    BlockDriverAIOCB *file_acb;
    bool need_alloc = ret != COW_CLUSTER_FOUND;

    if (ret == COW2_CLUSTER_ERROR) {
        goto err;
    }

    cow2_acb_build_qiov(acb, need_alloc);

    if (need_alloc) {
        if (cow2_alloc_clusters(s, 1, &offset) != 0) {
            goto err;
        }
    }

    /* Adjust offset into the cluster */
    offset += acb->cur_pos & s->header.cluster_size;

    file_acb = bdrv_aio_writev(s->file, offset / BDRV_SECTOR_SIZE,
                               &acb->cur_qiov,
                               acb->cur_qiov.size / BDRV_SECTOR_SIZE,
                               cbs[ret], acb);
    if (!file_acb) {
        goto err;
    }
    return;

err:
    cow2_aio_write_next_cluster(acb, -EIO);
}

/**
 * Begin write-out of next cluster or complete a request
 */
static void cow2_aio_write_next_cluster(void *opaque, int ret)
{
    Cow2AIOCB *acb = opaque;
    BDRVCow2State *s = acb->common.bs->opaque;

    /* Handle I/O error */
    if (ret) {
        cow2_aio_complete(acb, ret);
        return;
    }

    acb->cur_pos += acb->cur_qiov.size;

    /* Complete request */
    if (acb->cur_pos >= acb->end_pos) {
        cow2_aio_complete(acb, 0);
        return;
    }

    /* Find next cluster and write it out */
    cluster = acb->cur_pos & ~(uint64_t)s->header.cluster_size;
    cow2_find_cluster(s, cluster, cow2_aio_write_data, acb);
}

static BlockDriverAIOCB *bdrv_cow2_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    Cow2AIOCB *acb = qemu_aio_get(&cow2_aio_pool, bs, cb, opaque);

    trace_cow2_aio_writev(bs->opaque, acb, sector_num, nb_sectors, opaque);

    acb->qiov = qiov;
    acb->cur_pos = (uint64_t)sector_num * BDRV_SECTOR_SIZE;
    acb->end_pos = acb->cur_pos + nb_sectors * BDRV_SECTOR_SIZE;
    qemu_iovec_init(&acb->cur_qiov, qiov->niov);

    cow2_aio_write_next_cluster(acb, 0);
    return &acb->common;
}

static BlockDriverAIOCB *bdrv_cow2_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_flush(bs->file, cb, opaque);
}

static int bdrv_cow2_truncate(BlockDriverState *bs, int64_t offset)
{
    return -ENOTSUP; /* TODO */
}

static int64_t bdrv_cow2_getlength(BlockDriverState *bs)
{
    BDRVCow2State *s = bs->opaque;
    return s->header.image_size;
}

static int bdrv_cow2_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVCow2State *s = bs->opaque;

    memset(bdi, 0, sizeof *bdi);
    bdi->cluster_size = s->header.cluster_size;
    return 0;
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
