/*
 * Block driver for vmstate format
 *
 * Copyright (c) 2009 Nokia Corporation
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
#include "qemu-common.h"
#include "block_int.h"

#define VMSTATE_MAGIC 0x564D53544154451ALL
#define VMSTATE_VERSION 1

typedef struct VMStateHeader {
    uint64_t magic;
    uint32_t version;
    uint64_t state_offset;
    uint64_t state_size;
} VMStateHeader;

typedef struct BDRVVmState {
    int fd;
    uint64_t state_offset;
    uint64_t state_size;
    uint64_t write_offset;
} BDRVVMState;

static int vmstate_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const VMStateHeader *header = (const VMStateHeader *)buf;
    if (buf_size >= sizeof(VMStateHeader) &&
        be64_to_cpu(header->magic) == VMSTATE_MAGIC &&
        be32_to_cpu(header->version) == VMSTATE_VERSION)
        return 100;
    return 0;
}

static int vmstate_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVVMState *s = bs->opaque;
    VMStateHeader header;
    
    s->fd = open(filename, O_RDWR | O_BINARY);
    if (s->fd < 0)
        return -errno;
    if (read(s->fd, &header, sizeof(header)) == sizeof(header) &&
        be64_to_cpu(header.magic) == VMSTATE_MAGIC &&
        be32_to_cpu(header.version) == VMSTATE_VERSION) {
        s->state_offset = be64_to_cpu(header.state_offset);
        s->state_size = be64_to_cpu(header.state_size);
        
        s->write_offset = s->state_offset;
        return 0;
    }
    close(s->fd);
    return -EIO;
}

static void vmstate_flush(BlockDriverState *bs)
{
}

static void vmstate_close(BlockDriverState *bs)
{
    BDRVVMState *s = bs->opaque;
    
    vmstate_flush(bs);
    close(s->fd);
}

static int vmstate_create(const char *filename, int64_t total_size,
                          const char *backing_file, int flags)
{
    VMStateHeader header;
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0)
        return -EIO;
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be64(VMSTATE_MAGIC);
    header.version = cpu_to_be32(VMSTATE_VERSION);
    header.state_offset = cpu_to_be64(sizeof(header));
    write(fd, &header, sizeof(header));
    close(fd);
    return 0;
}

static int vmstate_refresh_header(BDRVVMState *s)
{
    VMStateHeader header;
    
    if (!lseek(s->fd, 0, SEEK_SET) &&
        read(s->fd, &header, sizeof(header)) == sizeof(header)) {
        header.state_size = cpu_to_be64(s->state_size);
        if (!lseek(s->fd, 0, SEEK_SET) &&
            write(s->fd, &header, sizeof(header)) == sizeof(header))
            return 0;
    }
    return -EIO;
}

static int vmstate_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVVMState *s = bs->opaque;
    
    bdi->cluster_size = 0;//VMSTATE_BLOCK_SIZE;
    bdi->vm_state_offset = s->state_offset;
    bdi->highest_alloc = s->state_offset;
    bdi->num_free_bytes = 0;
    return 0;
}

static int vmstate_read(BlockDriverState *bs, int64_t sector_num,
                        uint8_t *buf, int nb_sectors)
{
    BDRVVMState *s = bs->opaque;
    
    if (lseek(s->fd, sector_num * 512, SEEK_SET) != sector_num * 512)
        return -EIO;
    return read(s->fd, buf, nb_sectors * 512);
}

static int vmstate_write(BlockDriverState *bs, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors)
{
    BDRVVMState *s = bs->opaque;
    
    if (lseek(s->fd, sector_num * 512, SEEK_SET) != sector_num * 512)
        return -EIO;
    return write(s->fd, buf, nb_sectors * 512);
}

static int vmstate_snapshot_goto(BlockDriverState *bs, const char *snapshot_id)
{
    BDRVVMState *s = bs->opaque;

    return s->state_size ? 0 : -ENOENT;
}

static int vmstate_snapshot_delete(BlockDriverState *bs, const char *snapshot_id)
{
    BDRVVMState *s = bs->opaque;
    
    if (s->state_size) {
        s->state_size = 0;
        vmstate_refresh_header(s);
        if (!lseek(s->fd, 0, SEEK_SET))
            return ftruncate(s->fd, sizeof(VMStateHeader));
    }
    return -ENOENT;
}

static int vmstate_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info)
{
    BDRVVMState *s = bs->opaque;
    
    if (s->state_size)
        vmstate_snapshot_delete(bs, NULL);
    s->state_size = sn_info->vm_state_size;
    s->write_offset = s->state_offset;
    return vmstate_refresh_header(s);
}

static int vmstate_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_tab)
{
    BDRVVMState *s = bs->opaque;
    QEMUSnapshotInfo *sn_info;
    
    sn_info = qemu_mallocz(sizeof(QEMUSnapshotInfo));
    if (s->state_size) {
        pstrcpy(sn_info->id_str, sizeof(sn_info->id_str), "vmstate");
        pstrcpy(sn_info->name, sizeof(sn_info->name), "vmstate");
        sn_info->vm_state_size = s->state_size;
    }
    *psn_tab = sn_info;
    return s->state_size ? 1 : 0;
}

static int64_t vmstate_getlength(BlockDriverState *bs)
{
    return 1LL << 63; /* big enough? */
}

BlockDriver bdrv_vmstate = {
    .format_name = "vmstate",
    .instance_size = sizeof(BDRVVMState),
    .bdrv_probe = vmstate_probe,
    .bdrv_open = vmstate_open,
    .bdrv_read = vmstate_read,
    .bdrv_write = vmstate_write,
    .bdrv_close = vmstate_close,
    .bdrv_create = vmstate_create,
    .bdrv_flush = vmstate_flush,
    .bdrv_getlength = vmstate_getlength,
    .bdrv_snapshot_create = vmstate_snapshot_create,
    .bdrv_snapshot_goto = vmstate_snapshot_goto,
    .bdrv_snapshot_delete = vmstate_snapshot_delete,
    .bdrv_snapshot_list = vmstate_snapshot_list,
    .bdrv_get_info = vmstate_get_info
};