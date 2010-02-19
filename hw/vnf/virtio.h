#ifndef QEMU_VNF_VIRTIO_H
#define QEMU_VNF_VIRTIO_H

#include <linux/virtio_ring.h>

#define MAX_VIRTQUEUE  4

struct virtio_device;

struct virtqueue
{
    int last_avail;
    struct vring vring;
    void (*handle_output)(struct virtio_device *vdev, struct virtqueue *vq);
};

struct virtio_transport_ops
{
    void (*notify)(struct virtio_device *vdev, struct virtqueue *vq);
    void *(*map)(struct virtio_device *vdev, uint64_t addr, uint32_t size);
    /* FIXME: add an unmap */
};

struct virtio_device
{
    int num_vqs;
    struct virtqueue vq[MAX_VIRTQUEUE];
    struct virtio_transport_ops *trans;
};

bool virtqueue_empty(struct virtqueue *vq);

void virtqueue_add_useds(struct virtqueue *vq,
                         int count,
                         unsigned *heads,
                         uint32_t *lengths);

void virtqueue_add_used(struct virtqueue *vq,
                        unsigned head,
                        uint32_t length);

unsigned virtio_next_avail(struct virtio_device *vdev,
                           struct virtqueue *vq,
                           struct iovec *out_sg,
                           struct iovec *in_sg,
                           unsigned *out_num,
                           unsigned *in_num);

void virtqueue_enable_notification(struct virtqueue *vq);

void virtqueue_disable_notification(struct virtqueue *vq);

void virtio_kick(struct virtio_device *vdev);

void virtio_notify(struct virtio_device *vdev, struct virtqueue *vq);

struct virtqueue *virtio_init_vq(struct virtio_device *vdev,
                                 int vq_num, uint64_t addr, int num,
                                 bool (*callback)(struct virtio_device *,
                                                  struct virtqueue *));

#endif
