/*
 * Virtio Block Device
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

#include "iov.h"
#include "qemu-common.h"
#include "virtio.h"
#include "pc.h"
#include "sysemu.h"
#include "cpu.h"
#include "monitor.h"
#include "balloon.h"
#include "virtio-balloon.h"
#include "kvm.h"
#include "qlist.h"
#include "qint.h"
#include "qstring.h"

#if defined(__linux__)
#include <sys/mman.h>
#endif

typedef struct VirtIOBalloon
{
    VirtIODevice vdev;
    VirtQueue *ivq, *dvq, *svq;
    uint32_t num_pages;
    uint32_t actual;
} VirtIOBalloon;

static VirtIOBalloon *to_virtio_balloon(VirtIODevice *vdev)
{
    return (VirtIOBalloon *)vdev;
}

static void balloon_page(void *addr, int deflate)
{
#if defined(__linux__)
    if (!kvm_enabled() || kvm_has_sync_mmu())
        qemu_madvise(addr, TARGET_PAGE_SIZE,
                deflate ? QEMU_MADV_WILLNEED : QEMU_MADV_DONTNEED);
#endif
}

static void virtio_balloon_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = to_virtio_balloon(vdev);
    VirtQueueElement elem;

    while (virtqueue_pop(vq, &elem)) {
        size_t offset = 0;
        uint32_t pfn;

        while (iov_to_buf(elem.out_sg, elem.out_num, &pfn, offset, 4) == 4) {
            ram_addr_t pa;
            ram_addr_t addr;

            pa = (ram_addr_t)ldl_p(&pfn) << VIRTIO_BALLOON_PFN_SHIFT;
            offset += 4;

            addr = cpu_get_physical_page_desc(pa);
            if ((addr & ~TARGET_PAGE_MASK) != IO_MEM_RAM)
                continue;

            /* Using qemu_get_ram_ptr is bending the rules a bit, but
               should be OK because we only want a single page.  */
            balloon_page(qemu_get_ram_ptr(addr), !!(vq == s->dvq));
        }

        virtqueue_push(vq, &elem, offset);
        virtio_notify(vdev, vq);
    }
}

static void virtio_balloon_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;

    config.num_pages = cpu_to_le32(dev->num_pages);
    config.actual = cpu_to_le32(dev->actual);

    memcpy(config_data, &config, 8);
}

static void virtio_balloon_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;
    memcpy(&config, config_data, 8);
    dev->actual = config.actual;
}

static uint32_t virtio_balloon_get_features(VirtIODevice *vdev, uint32_t f)
{
    return 0;
}

static void virtio_balloon_stats(void *opaque, BalloonInfo *info)
{
    VirtIOBalloon *dev = opaque;
    info->actual = ram_size - ((uint64_t) dev->actual <<
                               VIRTIO_BALLOON_PFN_SHIFT);
}

static void virtio_balloon_to_target(void *opaque, ram_addr_t target)
{
    VirtIOBalloon *dev = opaque;

    if (target > ram_size) {
        target = ram_size;
    }

    if (target) {
        dev->num_pages = (ram_size - target) >> VIRTIO_BALLOON_PFN_SHIFT;
        virtio_notify_config(&dev->vdev);
    }
}

static void virtio_balloon_save(QEMUFile *f, void *opaque)
{
    VirtIOBalloon *s = opaque;

    virtio_save(&s->vdev, f);

    qemu_put_be32(f, s->num_pages);
    qemu_put_be32(f, s->actual);
}

static int virtio_balloon_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOBalloon *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    virtio_load(&s->vdev, f);

    s->num_pages = qemu_get_be32(f);
    s->actual = qemu_get_be32(f);
    return 0;
}

VirtIODevice *virtio_balloon_init(DeviceState *dev)
{
    VirtIOBalloon *s;

    s = (VirtIOBalloon *)virtio_common_init("virtio-balloon",
                                            VIRTIO_ID_BALLOON,
                                            8, sizeof(VirtIOBalloon));

    s->vdev.get_config = virtio_balloon_get_config;
    s->vdev.set_config = virtio_balloon_set_config;
    s->vdev.get_features = virtio_balloon_get_features;

    s->ivq = virtio_add_queue(&s->vdev, 128, virtio_balloon_handle_output);
    s->dvq = virtio_add_queue(&s->vdev, 128, virtio_balloon_handle_output);

    qemu_add_balloon_handler(virtio_balloon_to_target, s);
    qemu_add_balloon_stats_handler(virtio_balloon_stats, s);

    register_savevm(dev, "virtio-balloon", -1, 1,
                    virtio_balloon_save, virtio_balloon_load, s);

    return &s->vdev;
}
