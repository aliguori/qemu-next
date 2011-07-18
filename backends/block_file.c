/*
 * Block driver for RAW files (posix)
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "block/raw-posix-aio.h"
#include "block_file.h"

#ifdef __sun__
#define _POSIX_PTHREAD_SEMANTICS 1
#include <sys/dkio.h>
#endif
#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/param.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#endif
#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/disk.h>
#include <sys/cdio.h>
#endif

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#endif

#ifdef __NetBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/disk.h>
#endif

#ifdef __DragonFly__
#include <sys/ioctl.h>
#include <sys/diskslice.h>
#endif

#ifdef CONFIG_XFS
#include <xfs/xfs.h>
#endif

/* OS X does not have O_DSYNC */
#ifndef O_DSYNC
#ifdef O_SYNC
#define O_DSYNC O_SYNC
#elif defined(O_FSYNC)
#define O_DSYNC O_FSYNC
#endif
#endif

/* Approximate O_DIRECT with O_DSYNC if O_DIRECT isn't available */
#ifndef O_DIRECT
#define O_DIRECT O_DSYNC
#endif

#define MAX_BLOCKSIZE	4096

static int block_file_open(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    int fd, ret;

    s->open_flags |= O_BINARY;
    s->open_flags &= ~O_ACCMODE;
    if (bs->bdrv_flags & BDRV_O_RDWR) {
        s->open_flags |= O_RDWR;
    } else {
        s->open_flags |= O_RDONLY;
    }

    /* Use O_DSYNC for write-through caching, no flags for write-back caching,
     * and O_DIRECT for no caching. */
    if ((bs->bdrv_flags & BDRV_O_NOCACHE)) {
        s->open_flags |= O_DIRECT;
    }

    if (!(bs->bdrv_flags & BDRV_O_CACHE_WB)) {
        s->open_flags |= O_DSYNC;
    }

    s->fd = -1;
    fd = qemu_open(s->filename, s->open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS) {
            ret = -EACCES;
        }
        return ret;
    }
    s->fd = fd;
    s->aligned_buf = NULL;

    if ((bs->bdrv_flags & BDRV_O_NOCACHE)) {
        /*
         * Allocate a buffer for read/modify/write cycles.  Chose the size
         * pessimistically as we don't know the block size yet.
         */
        s->aligned_buf_size = 32 * MAX_BLOCKSIZE;
        s->aligned_buf = qemu_memalign(MAX_BLOCKSIZE, s->aligned_buf_size);
        if (s->aligned_buf == NULL) {
            goto out_close;
        }
    }

#ifdef CONFIG_LINUX_AIO
    if ((bs->bdrv_flags & (BDRV_O_NOCACHE|BDRV_O_NATIVE_AIO)) ==
        (BDRV_O_NOCACHE|BDRV_O_NATIVE_AIO)) {

        /* We're falling back to POSIX AIO in some cases */
        paio_init();

        s->aio_ctx = laio_init();
        if (!s->aio_ctx) {
            goto out_free_buf;
        }
        s->use_aio = 1;
    } else
#endif
    {
        if (paio_init() < 0) {
            goto out_free_buf;
        }
#ifdef CONFIG_LINUX_AIO
        s->use_aio = 0;
#endif
    }

#ifdef CONFIG_XFS
    if (platform_test_xfs_fd(s->fd)) {
        s->is_xfs = 1;
    }
#endif

    return 0;

out_free_buf:
    qemu_vfree(s->aligned_buf);
out_close:
    close(fd);
    return -errno;
}

/*
 * offset and count are in bytes, but must be multiples of 512 for files
 * opened with O_DIRECT. buf must be aligned to 512 bytes then.
 *
 * This function may be called without alignment if the caller ensures
 * that O_DIRECT is not in effect.
 */
static int block_file_pread_aligned(BlockDev *bs,
                                    int64_t offset,
                                    uint8_t *buf,
                                    int count)
{
    BlockFile *s = BLOCK_FILE(bs);
    int ret;

    ret = pread(s->fd, buf, count, offset);
    if (ret == count) {
        return ret;
    }

    /* Allow reads beyond the end (needed for pwrite) */
    if ((ret == 0) && bs->growable) {
        int64_t size = block_dev_getlength(bs);
        if (offset >= size) {
            memset(buf, 0, count);
            return count;
        }
    }

    return  (ret < 0) ? -errno : ret;
}

/*
 * offset and count are in bytes, but must be multiples of the sector size
 * for files opened with O_DIRECT. buf must be aligned to sector size bytes
 * then.
 *
 * This function may be called without alignment if the caller ensures
 * that O_DIRECT is not in effect.
 */
static int block_file_pwrite_aligned(BlockDev *bs,
                                     int64_t offset,
                                     const uint8_t *buf,
                                     int count)
{
    BlockFile *s = BLOCK_FILE(bs);
    int ret;

    ret = pwrite(s->fd, buf, count, offset);
    if (ret == count) {
        return ret;
    }

    return  (ret < 0) ? -errno : ret;
}


/*
 * offset and count are in bytes and possibly not aligned. For files opened
 * with O_DIRECT, necessary alignments are ensured before calling
 * raw_pread_aligned to do the actual read.
 */
static int block_file_pread(BlockDev *bs,
                            int64_t offset,
                            uint8_t *buf,
                            int count)
{
    BlockFile *s = BLOCK_FILE(bs);
    unsigned sector_mask = bs->buffer_alignment - 1;
    int size, ret, shift, sum;

    sum = 0;

    if (s->aligned_buf != NULL)  {
        if (offset & sector_mask) {
            /* align offset on a sector size bytes boundary */

            shift = offset & sector_mask;
            size = (shift + count + sector_mask) & ~sector_mask;
            if (size > s->aligned_buf_size) {
                size = s->aligned_buf_size;
            }
            ret = block_file_pread_aligned(bs, offset - shift, s->aligned_buf, size);
            if (ret < 0) {
                return ret;
            }

            size = bs->buffer_alignment - shift;
            if (size > count) {
                size = count;
            }
            memcpy(buf, s->aligned_buf + shift, size);

            buf += size;
            offset += size;
            count -= size;
            sum += size;

            if (count == 0) {
                return sum;
            }
        }

        if (count & sector_mask || (uintptr_t) buf & sector_mask) {
            /* read on aligned buffer */
            while (count) {
                size = (count + sector_mask) & ~sector_mask;
                if (size > s->aligned_buf_size) {
                    size = s->aligned_buf_size;
                }

                ret = block_file_pread_aligned(bs, offset, s->aligned_buf, size);
                if (ret < 0) {
                    return ret;
                } else if (ret == 0) {
                    fprintf(stderr, "raw_pread: read beyond end of file\n");
                    abort();
                }

                size = ret;
                if (size > count) {
                    size = count;
                }

                memcpy(buf, s->aligned_buf, size);

                buf += size;
                offset += size;
                count -= size;
                sum += size;
            }

            return sum;
        }
    }

    return block_file_pread_aligned(bs, offset, buf, count) + sum;
}

static int block_file_read(BlockDev *bs,
                           int64_t sector_num,
                           uint8_t *buf,
                           int nb_sectors)
{
    int ret;

    ret = block_file_pread(bs, sector_num * BDRV_SECTOR_SIZE, buf,
                           nb_sectors * BDRV_SECTOR_SIZE);
    if (ret == (nb_sectors * BDRV_SECTOR_SIZE))
        ret = 0;
    return ret;
}

/*
 * offset and count are in bytes and possibly not aligned. For files opened
 * with O_DIRECT, necessary alignments are ensured before calling
 * raw_pwrite_aligned to do the actual write.
 */
static int block_file_pwrite(BlockDev *bs,
                             int64_t offset,
                             const uint8_t *buf,
                             int count)
{
    BlockFile *s = BLOCK_FILE(bs);
    unsigned sector_mask = bs->buffer_alignment - 1;
    int size, ret, shift, sum;

    sum = 0;

    if (s->aligned_buf != NULL) {
        if (offset & sector_mask) {
            /* align offset on a sector size bytes boundary */
            shift = offset & sector_mask;
            ret = block_file_pread_aligned(bs, offset - shift, s->aligned_buf,
                                           bs->buffer_alignment);
            if (ret < 0) {
                return ret;
            }

            size = bs->buffer_alignment - shift;
            if (size > count) {
                size = count;
            }
            memcpy(s->aligned_buf + shift, buf, size);

            ret = block_file_pwrite_aligned(bs, offset - shift, s->aligned_buf,
                                            bs->buffer_alignment);
            if (ret < 0) {
                return ret;
            }

            buf += size;
            offset += size;
            count -= size;
            sum += size;

            if (count == 0) {
                return sum;
            }
        }

        if (count & sector_mask || (uintptr_t) buf & sector_mask) {
            while ((size = (count & ~sector_mask)) != 0) {
                if (size > s->aligned_buf_size) {
                    size = s->aligned_buf_size;
                }

                memcpy(s->aligned_buf, buf, size);

                ret = block_file_pwrite_aligned(bs, offset, s->aligned_buf, size);
                if (ret < 0) {
                    return ret;
                }

                buf += ret;
                offset += ret;
                count -= ret;
                sum += ret;
            }

            /* here, count < sector_size because (count & ~sector_mask) == 0 */
            if (count) {
                ret = block_file_pread_aligned(bs, offset, s->aligned_buf,
                                               bs->buffer_alignment);
                if (ret < 0) {
                    return ret;
                }

                memcpy(s->aligned_buf, buf, count);

                ret = block_file_pwrite_aligned(bs, offset, s->aligned_buf,
                                                bs->buffer_alignment);
                if (ret < 0) {
                    return ret;
                }

                if (count < ret) {
                    ret = count;
                }

                sum += ret;
            }
            return sum;
        }
    }
    return block_file_pwrite_aligned(bs, offset, buf, count) + sum;
}

static int block_file_write(BlockDev *bs,
                            int64_t sector_num,
                            const uint8_t *buf,
                            int nb_sectors)
{
    int ret;

    ret = block_file_pwrite(bs, sector_num * BDRV_SECTOR_SIZE, buf,
                            nb_sectors * BDRV_SECTOR_SIZE);
    if (ret == (nb_sectors * BDRV_SECTOR_SIZE)) {
        ret = 0;
    }

    return ret;
}

/*
 * Check if all memory in this vector is sector aligned.
 */
static int qiov_is_aligned(BlockDev *bs, QEMUIOVector *qiov)
{
    int i;

    for (i = 0; i < qiov->niov; i++) {
        if ((uintptr_t)qiov->iov[i].iov_base % bs->buffer_alignment) {
            return 0;
        }
    }

    return 1;
}

static BlockDriverAIOCB *block_file_aio_submit(BlockDev *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque,
                                               int type)
{
    BlockFile *s = BLOCK_FILE(bs);

    /*
     * If O_DIRECT is used the buffer needs to be aligned on a sector
     * boundary.  Check if this is the case or telll the low-level
     * driver that it needs to copy the buffer.
     */
    if (s->aligned_buf) {
        if (!qiov_is_aligned(bs, qiov)) {
            type |= QEMU_AIO_MISALIGNED;
#ifdef CONFIG_LINUX_AIO
        } else if (s->use_aio) {
            return laio_submit(bs, s->aio_ctx, s->fd, sector_num, qiov,
                               nb_sectors, cb, opaque, type);
#endif
        }
    }

    return paio_submit(NULL, s->fd, sector_num, qiov, nb_sectors,
                       cb, opaque, type);
}

static BlockDriverAIOCB *block_file_aio_readv(BlockDev *bs,
                                              int64_t sector_num,
                                              QEMUIOVector *qiov,
                                              int nb_sectors,
                                              BlockDriverCompletionFunc *cb,
                                              void *opaque)
{
    return block_file_aio_submit(bs, sector_num, qiov, nb_sectors,
                                 cb, opaque, QEMU_AIO_READ);
}

static BlockDriverAIOCB *block_file_aio_writev(BlockDev *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque)
{
    return block_file_aio_submit(bs, sector_num, qiov, nb_sectors,
                                 cb, opaque, QEMU_AIO_WRITE);
}

static BlockDriverAIOCB *block_file_aio_flush(BlockDev *bs,
                                              BlockDriverCompletionFunc *cb,
                                              void *opaque)
{
    BlockFile *s = BLOCK_FILE(bs);

    return paio_submit(NULL, s->fd, 0, NULL, 0, cb, opaque, QEMU_AIO_FLUSH);
}

static void block_file_close(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);

    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
        if (s->aligned_buf != NULL) {
            qemu_vfree(s->aligned_buf);
        }
    }
}

static int block_file_truncate(BlockDev *bs, int64_t offset)
{
    BlockFile *s = BLOCK_FILE(bs);

    if (ftruncate(s->fd, offset) < 0) {
        return -errno;
    }

    return 0;
}

#ifdef __OpenBSD__
static int64_t block_file_getlength(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    int fd = s->fd;
    struct stat st;

    if (fstat(fd, &st)) {
        return -1;
    }

    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        struct disklabel dl;

        if (ioctl(fd, DIOCGDINFO, &dl)) {
            return -1;
        }
        return (uint64_t)dl.d_secsize *
            dl.d_partitions[DISKPART(st.st_rdev)].p_size;
    } else {
        return st.st_size;
    }
}
#elif defined(__NetBSD__)
static int64_t block_file_getlength(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    int fd = s->fd;
    struct stat st;

    if (fstat(fd, &st)) {
        return -1;
    }
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        struct dkwedge_info dkw;

        if (ioctl(fd, DIOCGWEDGEINFO, &dkw) != -1) {
            return dkw.dkw_size * 512;
        } else {
            struct disklabel dl;

            if (ioctl(fd, DIOCGDINFO, &dl)) {
                return -1;
            }
            return (uint64_t)dl.d_secsize *
                dl.d_partitions[DISKPART(st.st_rdev)].p_size;
        }
    } else {
        return st.st_size;
    }
}
#elif defined(__sun__)
static int64_t block_file_getlength(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    struct dk_minfo minfo;
    int ret;

    /*
     * Use the DKIOCGMEDIAINFO ioctl to read the size.
     */
    ret = ioctl(s->fd, DKIOCGMEDIAINFO, &minfo);
    if (ret != -1) {
        return minfo.dki_lbsize * minfo.dki_capacity;
    }

    /*
     * There are reports that lseek on some devices fails, but
     * irc discussion said that contingency on contingency was overkill.
     */
    return lseek(s->fd, 0, SEEK_END);
}
#elif defined(CONFIG_BSD)
static int64_t block_file_getlength(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    int fd = s->fd;
    int64_t size;
    struct stat sb;
#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
    int reopened = 0;
#endif
    int ret;

#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
again:
#endif
    if (!fstat(fd, &sb) && (S_IFCHR & sb.st_mode)) {
#ifdef DIOCGMEDIASIZE
	if (ioctl(fd, DIOCGMEDIASIZE, (off_t *)&size))
#elif defined(DIOCGPART)
        {
            struct partinfo pi;
            if (ioctl(fd, DIOCGPART, &pi) == 0) {
                size = pi.media_size;
            } else {
                size = 0;
            }
        }
        if (size == 0)
#endif
#ifdef CONFIG_COCOA
        size = LONG_LONG_MAX;
#else
        size = lseek(fd, 0LL, SEEK_END);
#endif
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        switch(s->type) {
        case FTYPE_CD:
            /* XXX FreeBSD acd returns UINT_MAX sectors for an empty drive */
            if (size == 2048LL * (unsigned)-1) {
                size = 0;
            }
            /* XXX no disc?  maybe we need to reopen... */
            if (size <= 0 && !reopened && cdrom_reopen(bs) >= 0) {
                reopened = 1;
                goto again;
            }
        }
#endif
    } else {
        size = lseek(fd, 0, SEEK_END);
    }
    return size;
}
#else
static int64_t block_file_getlength(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);

    return lseek(s->fd, 0, SEEK_END);
}
#endif

static int block_file_flush(BlockDev *bs)
{
    BlockFile *s = BLOCK_FILE(bs);
    return qemu_fdatasync(s->fd);
}

#ifdef CONFIG_XFS
static int xfs_discard(BlockFile *s, int64_t sector_num, int nb_sectors)
{
    struct xfs_flock64 fl;

    memset(&fl, 0, sizeof(fl));
    fl.l_whence = SEEK_SET;
    fl.l_start = sector_num << 9;
    fl.l_len = (int64_t)nb_sectors << 9;

    if (xfsctl(NULL, s->fd, XFS_IOC_UNRESVSP64, &fl) < 0) {
        DEBUG_BLOCK_PRINT("cannot punch hole (%s)\n", strerror(errno));
        return -errno;
    }

    return 0;
}
#endif

static int block_file_discard(BlockDev *bs, int64_t sector_num, int nb_sectors)
{
#ifdef CONFIG_XFS
    BlockFile *s = BLOCK_FILE(bs);

    if (s->is_xfs) {
        return xfs_discard(s, sector_num, nb_sectors);
    }
#endif

    return 0;
}

const char *block_file_get_filename(BlockFile *obj)
{
    return obj->filename;
}

void block_file_set_filename(BlockFile *obj, const char *value)
{
    qemu_free(obj->filename);
    obj->filename = qemu_strdup(value);
}

static void block_file_init(TypeInstance *inst)
{
    BlockFile *s = BLOCK_FILE(inst);

    plug_add_property_str(PLUG(s), "filename",
                          (PlugPropertyGetterStr *)block_file_get_filename,
                          (PlugPropertySetterStr *)block_file_set_filename,
                          PROP_F_READWRITE);
}

static void block_file_class_init(TypeClass *class)
{
    BlockDevClass *bdc = BLOCK_DEV_CLASS(class);

    bdc->open = block_file_open;
    bdc->read = block_file_read;
    bdc->write = block_file_write;
    bdc->close = block_file_close;
    bdc->flush = block_file_flush;
    bdc->discard = block_file_discard;
    bdc->aio_readv = block_file_aio_readv;
    bdc->aio_writev = block_file_aio_writev;
    bdc->aio_flush = block_file_aio_flush;
    bdc->truncate = block_file_truncate;
    bdc->getlength = block_file_getlength;
}

static TypeInfo block_file_type_info = {
    .name = TYPE_BLOCK_FILE,
    .parent = TYPE_BLOCK_DEV,
    .instance_size = sizeof(BlockFile),
    .class_init = block_file_class_init,
    .instance_init = block_file_init,
};

static void register_backends(void)
{
    type_register_static(&block_file_type_info);
}

device_init(register_backends);

