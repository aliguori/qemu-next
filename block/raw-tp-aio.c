/*
 * QEMU posix-aio emulation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <sys/ioctl.h>

#include "osdep.h"
#include "sysemu.h"
#include "qemu-common.h"
#include "trace.h"
#include "block_int.h"
#include "qemu-thread.h"

#include "block/raw-aio.h"

typedef enum AioState {
    INACTIVE,
    CANCELLED,
    ACTIVE,
    COMPLETED
} AioState;

typedef struct AioAiocb {
    BlockDriverAIOCB common;
    int aio_fildes;
    union {
        struct iovec *aio_iov;
        void *aio_ioctl_buf;
    };
    int aio_niov;
    union {
        size_t aio_nbytes;
        long aio_ioctl_cmd;
    };
    off_t aio_offset;
    int aio_type;
    ssize_t ret;
    int async_context_id;

    /* This state can only be set/get when the aio pool lock is held */
    AioState state;
} AioAiocb;

typedef struct AioPool {
    GThreadPool *pool;
    int rfd, wfd;
    GList *requests;

    /* If this turns out to be contended, push to a per-request lock */
    GMutex *lock;
} AioPool;

static AioPool aio_pool;

#ifdef CONFIG_PREADV
static int preadv_present = 1;
#else
static int preadv_present = 0;
#endif

static ssize_t handle_aiocb_ioctl(AioAiocb *aiocb)
{
    int ret;

    ret = ioctl(aiocb->aio_fildes, aiocb->aio_ioctl_cmd, aiocb->aio_ioctl_buf);
    if (ret == -1)
        return -errno;

    /*
     * This looks weird, but the aio code only consideres a request
     * successful if it has written the number full number of bytes.
     *
     * Now we overload aio_nbytes as aio_ioctl_cmd for the ioctl command,
     * so in fact we return the ioctl command here to make posix_aio_read()
     * happy..
     */
    return aiocb->aio_nbytes;
}

static ssize_t handle_aiocb_flush(AioAiocb *aiocb)
{
    int ret;

    ret = qemu_fdatasync(aiocb->aio_fildes);
    if (ret == -1)
        return -errno;
    return 0;
}

#ifdef CONFIG_PREADV

static ssize_t
qemu_preadv(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return preadv(fd, iov, nr_iov, offset);
}

static ssize_t
qemu_pwritev(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return pwritev(fd, iov, nr_iov, offset);
}

#else

static ssize_t
qemu_preadv(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return -ENOSYS;
}

static ssize_t
qemu_pwritev(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return -ENOSYS;
}

#endif

static ssize_t handle_aiocb_rw_vector(AioAiocb *aiocb)
{
    size_t offset = 0;
    ssize_t len;

    do {
        if (aiocb->aio_type & QEMU_AIO_WRITE)
            len = qemu_pwritev(aiocb->aio_fildes,
                               aiocb->aio_iov,
                               aiocb->aio_niov,
                               aiocb->aio_offset + offset);
         else
            len = qemu_preadv(aiocb->aio_fildes,
                              aiocb->aio_iov,
                              aiocb->aio_niov,
                              aiocb->aio_offset + offset);
    } while (len == -1 && errno == EINTR);

    if (len == -1)
        return -errno;
    return len;
}

static ssize_t handle_aiocb_rw_linear(AioAiocb *aiocb, char *buf)
{
    ssize_t offset = 0;
    ssize_t len;

    while (offset < aiocb->aio_nbytes) {
         if (aiocb->aio_type & QEMU_AIO_WRITE)
             len = pwrite(aiocb->aio_fildes,
                          (const char *)buf + offset,
                          aiocb->aio_nbytes - offset,
                          aiocb->aio_offset + offset);
         else
             len = pread(aiocb->aio_fildes,
                         buf + offset,
                         aiocb->aio_nbytes - offset,
                         aiocb->aio_offset + offset);

         if (len == -1 && errno == EINTR)
             continue;
         else if (len == -1) {
             offset = -errno;
             break;
         } else if (len == 0)
             break;

         offset += len;
    }

    return offset;
}

static ssize_t handle_aiocb_rw(AioAiocb *aiocb)
{
    ssize_t nbytes;
    char *buf;

    if (!(aiocb->aio_type & QEMU_AIO_MISALIGNED)) {
        /*
         * If there is just a single buffer, and it is properly aligned
         * we can just use plain pread/pwrite without any problems.
         */
        if (aiocb->aio_niov == 1)
             return handle_aiocb_rw_linear(aiocb, aiocb->aio_iov->iov_base);

        /*
         * We have more than one iovec, and all are properly aligned.
         *
         * Try preadv/pwritev first and fall back to linearizing the
         * buffer if it's not supported.
         */
        if (preadv_present) {
            nbytes = handle_aiocb_rw_vector(aiocb);
            if (nbytes == aiocb->aio_nbytes)
                return nbytes;
            if (nbytes < 0 && nbytes != -ENOSYS)
                return nbytes;
            preadv_present = 0;
        }

        /*
         * XXX(hch): short read/write.  no easy way to handle the reminder
         * using these interfaces.  For now retry using plain
         * pread/pwrite?
         */
    }

    /*
     * Ok, we have to do it the hard way, copy all segments into
     * a single aligned buffer.
     */
    buf = qemu_blockalign(aiocb->common.bs, aiocb->aio_nbytes);
    if (aiocb->aio_type & QEMU_AIO_WRITE) {
        char *p = buf;
        int i;

        for (i = 0; i < aiocb->aio_niov; ++i) {
            memcpy(p, aiocb->aio_iov[i].iov_base, aiocb->aio_iov[i].iov_len);
            p += aiocb->aio_iov[i].iov_len;
        }
    }

    nbytes = handle_aiocb_rw_linear(aiocb, buf);
    if (!(aiocb->aio_type & QEMU_AIO_WRITE)) {
        char *p = buf;
        size_t count = aiocb->aio_nbytes, copy;
        int i;

        for (i = 0; i < aiocb->aio_niov && count; ++i) {
            copy = count;
            if (copy > aiocb->aio_iov[i].iov_len)
                copy = aiocb->aio_iov[i].iov_len;
            memcpy(aiocb->aio_iov[i].iov_base, p, copy);
            p     += copy;
            count -= copy;
        }
    }
    qemu_vfree(buf);

    return nbytes;
}

static void aio_routine(gpointer data, gpointer user_data)
{
    AioPool *s = user_data;
    AioAiocb *aiocb = data;
    ssize_t ret = 0;
    char ch = 0;
    AioState state;
    ssize_t len;

    g_mutex_lock(s->lock);
    if (aiocb->state != CANCELLED) {
        aiocb->state = ACTIVE;
    }
    state = aiocb->state;
    g_mutex_unlock(s->lock);
        
    if (state == CANCELLED) {
        return;
    }

    switch (aiocb->aio_type & QEMU_AIO_TYPE_MASK) {
    case QEMU_AIO_READ:
    case QEMU_AIO_WRITE:
        ret = handle_aiocb_rw(aiocb);
        break;
    case QEMU_AIO_FLUSH:
        ret = handle_aiocb_flush(aiocb);
        break;
    case QEMU_AIO_IOCTL:
        ret = handle_aiocb_ioctl(aiocb);
        break;
    default:
        fprintf(stderr, "invalid aio request (0x%x)\n", aiocb->aio_type);
        ret = -EINVAL;
        break;
    }

    aiocb->ret = ret;
    g_mutex_lock(s->lock);
    aiocb->state = COMPLETED;
    g_mutex_unlock(s->lock);

    do {
        len = write(s->wfd, &ch, sizeof(ch));
    } while (len == -1 && errno == EINTR);

    return;
}


static void qemu_paio_submit(AioAiocb *aiocb)
{
    AioPool *s = &aio_pool;
    aiocb->state = INACTIVE;
    aiocb->async_context_id = get_async_context_id();
    s->requests = g_list_append(s->requests, aiocb);
    g_thread_pool_push(s->pool, aiocb, NULL);
}

static void qemu_paio_cancel(BlockDriverAIOCB *acb)
{
    AioAiocb *aiocb = container_of(acb, AioAiocb, common);
    AioPool *s = &aio_pool;

    g_mutex_lock(s->lock);
    if (aiocb->state == INACTIVE) {
        aiocb->state = CANCELLED;
    }
    g_mutex_unlock(s->lock);
    
}

static AIOPool raw_aio_pool = {
    .aiocb_size         = sizeof(AioAiocb),
    .cancel             = qemu_paio_cancel,
};

BlockDriverAIOCB *tp_aio_submit(BlockDriverState *bs, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type)
{
    AioAiocb *acb;

    acb = qemu_aio_get(&raw_aio_pool, bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
    }
    acb->aio_nbytes = nb_sectors * 512;
    acb->aio_offset = sector_num * 512;

    trace_paio_submit(acb, opaque, sector_num, nb_sectors, type);
    qemu_paio_submit(acb);
    return &acb->common;
}

BlockDriverAIOCB *tp_aio_ioctl(BlockDriverState *bs, int fd,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    AioAiocb *acb;

    acb = qemu_aio_get(&raw_aio_pool, bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->aio_type = QEMU_AIO_IOCTL;
    acb->aio_fildes = fd;
    acb->aio_offset = 0;
    acb->aio_ioctl_buf = buf;
    acb->aio_ioctl_cmd = req;

    qemu_paio_submit(acb);
    return &acb->common;
}

static int paio_process_queue(void *opaque)
{
    AioPool *s = opaque;
    GList *i, *next_i;
    GList *completed_requests = NULL;
    int async_context_id = get_async_context_id();
    int did_work = 0;

    /* Search the list to build a list of completed requests, we do
     * this as it's own pass so that we minimize the time we're holding
     * the shared lock.
     */
    g_mutex_lock(s->lock);
    for (i = s->requests; i != NULL; i = next_i) {
        AioAiocb *aiocb = i->data;
        next_i = g_list_next(i);

        /* don't complete a request that isn't part of this async context */
        if (aiocb->async_context_id != async_context_id) {
            continue;
        }

        if (aiocb->state == CANCELLED || aiocb->state == COMPLETED) {
            s->requests = g_list_remove_link(s->requests, i);
            completed_requests = g_list_concat(completed_requests, i);
        }
    }
    g_mutex_unlock(s->lock);

    /* Dispatch any completed requests */
    for (i = completed_requests; i != NULL; i = g_list_next(i)) {
        AioAiocb *aiocb = i->data;
        if (aiocb->state == COMPLETED) {
            if (aiocb->ret == aiocb->aio_nbytes) {
                aiocb->ret = 0;
            }
            aiocb->common.cb(aiocb->common.opaque, aiocb->ret);
            did_work = 1;
        }
        qemu_aio_release(aiocb);
    }

    g_list_free(completed_requests);

    return did_work;
}

static int paio_io_flush(void *opaque)
{
    AioPool *s = opaque;
    if (s->requests == NULL) {
        return 0;
    }
    return 1;
}

static void paio_complete(void *opaque)
{
    AioPool *s = opaque;
    char buffer[1024];
    ssize_t len;

    /* Drain event queue */
    do {
        len = read(s->rfd, buffer, sizeof(buffer));
    } while (len == -1 && errno == EINTR);

    if (len == -1 && errno == EAGAIN) {
        return;
    }

    paio_process_queue(s);
}

int tp_aio_init(void)
{
    AioPool *s = &aio_pool;
    int fds[2];

    if (pipe(fds) == -1) {
        return -errno;
    }

    s->pool = g_thread_pool_new(aio_routine, s, 64, FALSE, NULL);
    s->rfd = fds[0];
    s->wfd = fds[1];
    s->requests = NULL;
    s->lock = g_mutex_new();

    fcntl(s->wfd, F_SETFL, O_NONBLOCK);
    fcntl(s->rfd, F_SETFL, O_NONBLOCK);
    qemu_aio_set_fd_handler(s->rfd, paio_complete, NULL,
                            paio_io_flush, paio_process_queue, s);

    return 0;
}
