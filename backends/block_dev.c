#include "block_dev.h"

/**************************************************************/
/* sync block device emulation */

static void block_dev_rw_em_cb(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

#define NOT_DONE 0x7fffffff

static int block_dev_read_em(BlockDev *bs,
                            int64_t sector_num,
                            uint8_t *buf,
                            int nb_sectors)
{
    int async_ret;
    BlockDriverAIOCB *acb;
    struct iovec iov;
    QEMUIOVector qiov;

    async_context_push();

    async_ret = NOT_DONE;
    iov.iov_base = (void *)buf;
    iov.iov_len = nb_sectors * BDRV_SECTOR_SIZE;
    qemu_iovec_init_external(&qiov, &iov, 1);
    acb = block_dev_aio_readv(bs, sector_num, &qiov, nb_sectors,
                             block_dev_rw_em_cb, &async_ret);
    if (acb == NULL) {
        async_ret = -1;
        goto fail;
    }

    while (async_ret == NOT_DONE) {
        qemu_aio_wait();
    }


fail:
    async_context_pop();
    return async_ret;
}

static int block_dev_write_em(BlockDev *bs,
                             int64_t sector_num,
                             const uint8_t *buf,
                             int nb_sectors)
{
    int async_ret;
    BlockDriverAIOCB *acb;
    struct iovec iov;
    QEMUIOVector qiov;

    async_context_push();

    async_ret = NOT_DONE;
    iov.iov_base = (void *)buf;
    iov.iov_len = nb_sectors * BDRV_SECTOR_SIZE;
    qemu_iovec_init_external(&qiov, &iov, 1);
    acb = block_dev_aio_writev(bs, sector_num, &qiov, nb_sectors,
                              block_dev_rw_em_cb, &async_ret);
    if (acb == NULL) {
        async_ret = -1;
        goto fail;
    }
    while (async_ret == NOT_DONE) {
        qemu_aio_wait();
    }

fail:
    async_context_pop();
    return async_ret;
}

static int block_dev_flush_nop(BlockDev *bs)
{
    /*
     * Some block drivers always operate in either writethrough or unsafe mode
     * and don't support bdrv_flush therefore. Usually qemu doesn't know how
     * the server works (because the behaviour is hardcoded or depends on
     * server-side configuration), so we can't ensure that everything is safe
     * on disk. Returning an error doesn't work because that would break guests
     * even if the server operates in writethrough mode.
     *
     * Let's hope the user knows what he's doing.
     */
    return 0;
}

static int block_dev_is_allocated_default(BlockDev *bs,
                                         int64_t sector_num,
                                         int nb_sectors,
                                         int *pnum)
{
    int64_t n;

    if (sector_num >= bs->total_sectors) {
        *pnum = 0;
        return 0;
    }

    n = bs->total_sectors - sector_num;
    *pnum = (n < nb_sectors) ? (n) : (nb_sectors);
    return 1;
}

static void block_dev_realize(Plug *plug)
{
    BlockDev *obj = BLOCK_DEV(plug);
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(obj);

    plug_lock_all_properties(plug);
    bdc->open(obj);
}

static void block_dev_class_init(TypeClass *class)
{
    PlugClass *pc = PLUG_CLASS(class);
    BlockDevClass *bdc = BLOCK_DEV_CLASS(class);

    pc->realize = block_dev_realize;

    bdc->read = block_dev_read_em;
    bdc->write = block_dev_write_em;
    bdc->flush = block_dev_flush_nop;
    bdc->is_allocated = block_dev_is_allocated_default;
#if 0
    bdc->set_key = ;
    bdc->make_empty = ;
    bdc->aio_readv = ;
    bdc->aio_writev = ;
    bdc->aio_flush = ;
    bdc->discard = ;
    bdc->aio_multiwrite = ;
    bdc->merge_requests = ;
    bdc->truncate = ;
    bdc->getlength = ;
    bdc->write_compressed = ;
    bdc->snapshot_create = ;
    bdc->snapshot_goto = ;
    bdc->snapshot_delete = ;
    bdc->snapshot_list = ;
    bdc->snapshot_load_tmp = ;
    bdc->get_info = ;
    bdc->save_vmstate = ;
    bdc->load_vmstate = ;
    bdc->change_backing_file = ;
    bdc->is_inserted = ;
    bdc->media_changed = ;
    bdc->eject = ;
    bdc->set_locked = ;
    bdc->ioctl = ;
    bdc->aio_ioctl = ;
    bdc->check = ;
    bdc->debug_event = ;
    bdc->has_zero_init = ;
#endif
}

int block_dev_read(BlockDev *bs,
                  int64_t sector_num,
                  uint8_t *buf,
                  int nb_sectors)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->read(bs, sector_num, buf, nb_sectors);
}

int block_dev_write(BlockDev *bs,
                   int64_t sector_num,
                   const uint8_t *buf,
                   int nb_sectors)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->write(bs, sector_num, buf, nb_sectors);
}

int block_dev_flush(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->flush(bs);
}

int block_dev_is_allocated(BlockDev *bs,
                          int64_t sector_num,
                          int nb_sectors,
                          int *pnum)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->is_allocated(bs, sector_num, nb_sectors, pnum);
}

int block_dev_set_key(BlockDev *bs,
                     const char *key)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->set_key(bs, key);
}

int block_dev_make_empty(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->make_empty(bs);
}

/* aio */
BlockDriverAIOCB *block_dev_aio_readv(BlockDev *bs,
                                     int64_t sector_num,
                                     QEMUIOVector *qiov,
                                     int nb_sectors,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->aio_readv(bs, sector_num, qiov, nb_sectors, cb, opaque);
}

BlockDriverAIOCB *block_dev_aio_writev(BlockDev *bs,
                                      int64_t sector_num,
                                      QEMUIOVector *qiov,
                                      int nb_sectors,
                                      BlockDriverCompletionFunc *cb,
                                      void *opaque)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->aio_writev(bs, sector_num, qiov, nb_sectors, cb, opaque);
}

BlockDriverAIOCB *block_dev_aio_flush(BlockDev *bs,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->aio_flush(bs, cb, opaque);
}

int block_dev_discard(BlockDev *bs,
                     int64_t sector_num,
                     int nb_sectors)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->discard(bs, sector_num, nb_sectors);
}

int block_dev_aio_multiwrite(BlockDev *bs,
                            BlockRequest *reqs,
                            int num_reqs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->aio_multiwrite(bs, reqs, num_reqs);
}

int block_dev_merge_requests(BlockDev *bs,
                            BlockRequest *a,
                            BlockRequest *b)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->merge_requests(bs, a, b);
}

int block_dev_truncate(BlockDev *bs,
                      int64_t offset)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->truncate(bs, offset);
}

int64_t block_dev_getlength(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->getlength(bs);
}

int block_dev_write_compressed(BlockDev *bs,
                              int64_t sector_num,
                              const uint8_t *buf,
                              int nb_sectors)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->write_compressed(bs, sector_num, buf, nb_sectors);
}

int block_dev_snapshot_create(BlockDev *bs,
                             QEMUSnapshotInfo *sn_info)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->snapshot_create(bs, sn_info);
}

int block_dev_snapshot_goto(BlockDev *bs,
                           const char *snapshot_id)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->snapshot_goto(bs, snapshot_id);
}

int block_dev_snapshot_delete(BlockDev *bs,
                             const char *snapshot_id)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->snapshot_delete(bs, snapshot_id);
}

int block_dev_snapshot_list(BlockDev *bs,
                           QEMUSnapshotInfo **psn_info)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->snapshot_list(bs, psn_info);
}

int block_dev_snapshot_load_tmp(BlockDev *bs,
                               const char *snapshot_name)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->snapshot_load_tmp(bs, snapshot_name);
}

int block_dev_get_info(BlockDev *bs,
                      BlockDriverInfo *bdi)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->get_info(bs, bdi);
}

int block_dev_save_vmstate(BlockDev *bs,
                          const uint8_t *buf,
                          int64_t pos,
                          int size)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->save_vmstate(bs, buf, pos, size);
}

int block_dev_load_vmstate(BlockDev *bs,
                          uint8_t *buf,
                          int64_t pos,
                          int size)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->load_vmstate(bs, buf, pos, size);
}

int block_dev_change_backing_file(BlockDev *bs,
                                 const char *backing_file,
                                 const char *backing_fmt)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->change_backing_file(bs, backing_file, backing_fmt);
}

/* removable device specific */
int block_dev_is_inserted(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->is_inserted(bs);
}

int block_dev_media_changed(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->media_changed(bs);
}

int block_dev_eject(BlockDev *bs,
                   int eject_flag)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->eject(bs, eject_flag);
}

int block_dev_set_locked(BlockDev *bs,
                        int locked)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->set_locked(bs, locked);
}

/* to control generic scsi devices */
int block_dev_ioctl(BlockDev *bs,
                   unsigned long int req,
                   void *buf)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->ioctl(bs, req, buf);
}

BlockDriverAIOCB *block_dev_aio_ioctl(BlockDev *bs,
                                     unsigned long int req,
                                     void *buf,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->aio_ioctl(bs, req, buf, cb, opaque);
}

/*
 * Returns 0 for completed check, -errno for internal errors.
 * The check results are stored in result.
 */
int block_dev_check(BlockDev *bs,
                   BdrvCheckResult *result)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->check(bs, result);
}

void block_dev_debug_event(BlockDev *bs,
                          BlkDebugEvent event)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    bdc->debug_event(bs, event);
}

/*
 * Returns 1 if newly created images are guaranteed to contain only
 * zeros, 0 otherwise.
 */
int block_dev_has_zero_init(BlockDev *bs)
{
    BlockDevClass *bdc = BLOCK_DEV_GET_CLASS(bs);

    return bdc->has_zero_init(bs);
}

void block_dev_initialize(BlockDev *obj, const char *id)
{
    type_initialize(obj, TYPE_BLOCK_DEV, id);
}

void block_dev_finalize(BlockDev *obj)
{
    type_finalize(obj);
}

bool block_dev_get_cache(BlockDev *obj)
{
    return !!(obj->bdrv_flags & BDRV_O_NOCACHE);
}

void block_dev_set_cache(BlockDev *obj, bool value)
{
    if (value) {
        obj->bdrv_flags &= ~BDRV_O_NOCACHE;
    } else {
        obj->bdrv_flags |= BDRV_O_NOCACHE;
    }
}

bool block_dev_get_readonly(BlockDev *obj)
{
    return !!(obj->bdrv_flags & BDRV_O_RDWR);
}

void block_dev_set_readonly(BlockDev *obj, bool value)
{
    if (value) {
        obj->bdrv_flags &= ~BDRV_O_RDWR;
    } else {
        obj->bdrv_flags |= BDRV_O_RDWR;
    }
}

static void block_dev_init(TypeInstance *inst)
{
    BlockDev *obj = BLOCK_DEV(inst);

    plug_add_property_bool(PLUG(obj), "cache",
                           (PlugPropertyGetterBool *)block_dev_get_cache,
                           (PlugPropertySetterBool *)block_dev_set_cache,
                           PROP_F_READWRITE);

    plug_add_property_bool(PLUG(obj), "readonly",
                           (PlugPropertyGetterBool *)block_dev_get_readonly,
                           (PlugPropertySetterBool *)block_dev_set_readonly,
                           PROP_F_READWRITE);
}

static TypeInfo block_dev_type_info = {
    .name = TYPE_BLOCK_DEV,
    .parent = TYPE_PLUG,
    .instance_size = sizeof(BlockDev),
    .class_size = sizeof(BlockDevClass),
    .class_init = block_dev_class_init,
    .instance_init = block_dev_init,
};

static void register_backends(void)
{
    type_register_static(&block_dev_type_info);
}

device_init(register_backends);
