#ifndef BLOCK_DEV_H
#define BLOCK_DEV_H

#include "qemu/plug.h"
#include "block.h"

typedef struct BlockDev
{
    Plug parent;

    int bdrv_flags;

    int64_t total_sectors; /* if we are reading a disk image, give its
                              size in sectors */

    /* Whether the disk can expand beyond total_sectors */
    int growable;

    /* the memory alignment required for the buffers handled by this driver */
    int buffer_alignment;

    /* Whether to use the host page cache */
    bool cache;
} BlockDev;

typedef struct BlockDevClass
{
    Plug parent_class;

    int (*open)(BlockDev *bs);

    void (*close)(BlockDev *bs);

    int (*read)(BlockDev *bs,
                int64_t sector_num,
                uint8_t *buf,
                int nb_sectors);

    int (*write)(BlockDev *bs,
                 int64_t sector_num,
                 const uint8_t *buf,
                 int nb_sectors);

    int (*flush)(BlockDev *bs);

    int (*is_allocated)(BlockDev *bs,
                        int64_t sector_num,
                        int nb_sectors,
                        int *pnum);

    int (*set_key)(BlockDev *bs,
                   const char *key);

    int (*make_empty)(BlockDev *bs);

    /* aio */
    BlockDriverAIOCB *(*aio_readv)(BlockDev *bs,
                                   int64_t sector_num,
                                   QEMUIOVector *qiov,
                                   int nb_sectors,
                                   BlockDriverCompletionFunc *cb,
                                   void *opaque);

    BlockDriverAIOCB *(*aio_writev)(BlockDev *bs,
                                    int64_t sector_num,
                                    QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockDriverCompletionFunc *cb,
                                    void *opaque);

    BlockDriverAIOCB *(*aio_flush)(BlockDev *bs,
                                   BlockDriverCompletionFunc *cb,
                                   void *opaque);

    int (*discard)(BlockDev *bs,
                   int64_t sector_num,
                   int nb_sectors);

    int (*aio_multiwrite)(BlockDev *bs,
                          BlockRequest *reqs,
                          int num_reqs);

    int (*merge_requests)(BlockDev *bs,
                          BlockRequest* a,
                          BlockRequest *b);

    int (*truncate)(BlockDev *bs,
                    int64_t offset);

    int64_t (*getlength)(BlockDev *bs);

    int (*write_compressed)(BlockDev *bs,
                            int64_t sector_num,
                            const uint8_t *buf,
                            int nb_sectors);

    int (*snapshot_create)(BlockDev *bs,
                           QEMUSnapshotInfo *sn_info);

    int (*snapshot_goto)(BlockDev *bs,
                         const char *snapshot_id);

    int (*snapshot_delete)(BlockDev *bs,
                           const char *snapshot_id);

    int (*snapshot_list)(BlockDev *bs,
                         QEMUSnapshotInfo **psn_info);

    int (*snapshot_load_tmp)(BlockDev *bs,
                             const char *snapshot_name);

    int (*get_info)(BlockDev *bs,
                    BlockDriverInfo *bdi);

    int (*save_vmstate)(BlockDev *bs,
                        const uint8_t *buf,
                        int64_t pos,
                        int size);
    int (*load_vmstate)(BlockDev *bs,
                        uint8_t *buf,
                        int64_t pos,
                        int size);

    int (*change_backing_file)(BlockDev *bs,
                               const char *backing_file,
                               const char *backing_fmt);

    /* removable device specific */
    int (*is_inserted)(BlockDev *bs);

    int (*media_changed)(BlockDev *bs);

    int (*eject)(BlockDev *bs,
                 int eject_flag);

    int (*set_locked)(BlockDev *bs,
                      int locked);

    /* to control generic scsi devices */
    int (*ioctl)(BlockDev *bs,
                 unsigned long int req,
                 void *buf);

    BlockDriverAIOCB *(*aio_ioctl)(BlockDev *bs,
                                   unsigned long int req,
                                   void *buf,
                                   BlockDriverCompletionFunc *cb,
                                   void *opaque);

    /*
     * Returns 0 for completed check, -errno for internal errors.
     * The check results are stored in result.
     */
    int (*check)(BlockDev* bs,
                 BdrvCheckResult *result);

    void (*debug_event)(BlockDev *bs,
                        BlkDebugEvent event);

    /*
     * Returns 1 if newly created images are guaranteed to contain only
     * zeros, 0 otherwise.
     */
    int (*has_zero_init)(BlockDev *bs);
} BlockDevClass;

#define TYPE_BLOCK_DEV "block_dev"
#define BLOCK_DEV(obj) TYPE_CHECK(BlockDev, obj, TYPE_BLOCK_DEV)
#define BLOCK_DEV_CLASS(class) TYPE_CLASS_CHECK(BlockDevClass, class, TYPE_BLOCK_DEV)
#define BLOCK_DEV_GET_CLASS(obj) TYPE_GET_CLASS(BlockDevClass, obj, TYPE_BLOCK_DEV)

void block_dev_initialize(BlockDev *obj, const char *id);

void block_dev_finalize(BlockDev *obj);

bool block_dev_get_cache(BlockDev *obj);
void block_dev_set_cache(BlockDev *obj, bool value);

int block_dev_read(BlockDev *bs,
                  int64_t sector_num,
                  uint8_t *buf,
                  int nb_sectors);

int block_dev_write(BlockDev *bs,
                   int64_t sector_num,
                   const uint8_t *buf,
                   int nb_sectors);

int block_dev_flush(BlockDev *bs);

int block_dev_is_allocated(BlockDev *bs,
                          int64_t sector_num,
                          int nb_sectors,
                          int *pnum);

int block_dev_set_key(BlockDev *bs,
                     const char *key);

int block_dev_make_empty(BlockDev *bs);

/* aio */
BlockDriverAIOCB *block_dev_aio_readv(BlockDev *bs,
                                     int64_t sector_num,
                                     QEMUIOVector *qiov,
                                     int nb_sectors,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque);

BlockDriverAIOCB *block_dev_aio_writev(BlockDev *bs,
                                      int64_t sector_num,
                                      QEMUIOVector *qiov,
                                      int nb_sectors,
                                      BlockDriverCompletionFunc *cb,
                                      void *opaque);

BlockDriverAIOCB *block_dev_aio_flush(BlockDev *bs,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque);

int block_dev_discard(BlockDev *bs,
                     int64_t sector_num,
                     int nb_sectors);

int block_dev_aio_multiwrite(BlockDev *bs,
                            BlockRequest *reqs,
                            int num_reqs);

int block_dev_merge_requests(BlockDev *bs,
                            BlockRequest *a,
                            BlockRequest *b);

int block_dev_truncate(BlockDev *bs,
                      int64_t offset);

int64_t block_dev_getlength(BlockDev *bs);

int block_dev_write_compressed(BlockDev *bs,
                              int64_t sector_num,
                              const uint8_t *buf,
                              int nb_sectors);

int block_dev_snapshot_create(BlockDev *bs,
                             QEMUSnapshotInfo *sn_info);

int block_dev_snapshot_goto(BlockDev *bs,
                           const char *snapshot_id);

int block_dev_snapshot_delete(BlockDev *bs,
                             const char *snapshot_id);

int block_dev_snapshot_list(BlockDev *bs,
                           QEMUSnapshotInfo **psn_info);

int block_dev_snapshot_load_tmp(BlockDev *bs,
                               const char *snapshot_name);

int block_dev_get_info(BlockDev *bs,
                      BlockDriverInfo *bdi);

int block_dev_save_vmstate(BlockDev *bs,
                          const uint8_t *buf,
                          int64_t pos,
                          int size);

int block_dev_load_vmstate(BlockDev *bs,
                          uint8_t *buf,
                          int64_t pos,
                          int size);

int block_dev_change_backing_file(BlockDev *bs,
                                 const char *backing_file,
                                 const char *backing_fmt);

/* removable device specific */
int block_dev_is_inserted(BlockDev *bs);

int block_dev_media_changed(BlockDev *bs);

int block_dev_eject(BlockDev *bs,
                   int eject_flag);

int block_dev_set_locked(BlockDev *bs,
                        int locked);

/* to control generic scsi devices */
int block_dev_ioctl(BlockDev *bs,
                   unsigned long int req,
                   void *buf);

BlockDriverAIOCB *block_dev_aio_ioctl(BlockDev *bs,
                                     unsigned long int req,
                                     void *buf,
                                     BlockDriverCompletionFunc *cb,
                                     void *opaque);

/*
 * Returns 0 for completed check, -errno for internal errors.
 * The check results are stored in result.
 */
int block_dev_check(BlockDev *bs,
                   BdrvCheckResult *result);

void block_dev_debug_event(BlockDev *bs,
                          BlkDebugEvent event);

/*
 * Returns 1 if newly created images are guaranteed to contain only
 * zeros, 0 otherwise.
 */
int block_dev_has_zero_init(BlockDev *bs);

#endif
