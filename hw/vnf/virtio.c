#include "vnf/virtio.h"
#include "vnf/loop.h"
#include "vnf/util.h"

bool virtqueue_empty(struct virtqueue *vq)
{
    return (vq->last_avail == vq->vring.avail->idx);
}

void virtqueue_add_useds(struct virtqueue *vq,
                         int count,
                         unsigned *heads,
                         uint32_t *lengths)
{
    struct vring_used_elem *used;
    uint16_t idx;
    int i;

    idx = vq->vring.used->idx;
    for (i = 0; i < count; i++) {
        used = &vq->vring.used->ring[idx % vq->vring.num];
        used->id = heads[i];
        used->len = lengths[i];
        idx++;
    }
    vq->vring.used->idx = idx;
}

void virtqueue_add_used(struct virtqueue *vq,
                        unsigned head,
                        uint32_t length)
{
    unsigned heads[1] = { head };
    uint32_t lengths[1] = { length };

    virtqueue_add_useds(vq, 1, heads, lengths);
}

static unsigned vring_desc_next(struct vring_desc *desc, unsigned max)
{
    unsigned next;

    if (!(desc->flags & VRING_DESC_F_NEXT))
        return max;

    next = desc->next;
    wmb();

    return next;
}

unsigned virtio_next_avail(struct virtio_device *vdev,
                           struct virtqueue *vq,
                           struct iovec *out_sg,
                           struct iovec *in_sg,
                           unsigned *pout_num,
                           unsigned *pin_num)
{
    unsigned head, i, max;
    struct vring_desc *desc;
    unsigned out_num, in_num;

    if (virtqueue_empty(vq)) {
        return vq->vring.num;
    }

    head = vq->vring.avail->ring[vq->last_avail % vq->vring.num];
    vq->last_avail++;

    desc = vq->vring.desc;
    max = vq->vring.num;
    i = head;

    if ((desc[i].flags & VRING_DESC_F_INDIRECT)) {
        desc = vdev->trans->map(vdev->trans, desc[i].addr, desc[i].len);
        max = desc[i].len / sizeof(struct vring_desc);
        i = 0;
    }

    out_num = in_num = 0;
    do {
        if ((desc[i].flags & VRING_DESC_F_WRITE)) {
            if (in_num < *pin_num) {
                in_sg[in_num].iov_len = desc[i].len;
                in_sg[in_num].iov_base = vdev->trans->map(vdev->trans,
                                                          desc[i].addr,
                                                          desc[i].len);
                in_num++;
            }
        } else {
            if (out_num < *pout_num) {
                out_sg[out_num].iov_len = desc[i].len;
                out_sg[out_num].iov_base = vdev->trans->map(vdev->trans,
                                                            desc[i].addr,
                                                            desc[i].len);
                out_num++;
            }
        }
    } while ((i = vring_desc_next(&desc[i], max)) != max);

    *pout_num = out_num;
    *pin_num = in_num;

    return head;
}

void virtqueue_enable_notification(struct virtqueue *vq)
{
    vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;
}

void virtqueue_disable_notification(struct virtqueue *vq)
{
    vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
}

void virtio_kick(struct virtio_device *vdev)
{
    int i;
    bool did_work;

again:
    /* disable notification on all queues */
    for (i = 0; i < vdev->num_vqs; i++) {
        virtqueue_disable_notification(&vdev->vq[i]);
    }

    do {
        did_work = false;
        for (i = 0; i < vdev->num_vqs; i++) {
            if (vdev->vq[i].handle_output(vdev, &vdev->vq[i])) {
                virtio_notify_queue(vdev, &vdev->vq[i]);
                did_work = true;
            }
        }
    } while (did_work);

    /* enable notification on all queues */
    for (i = 0; i < vdev->num_vqs; i++) {
        virtqueue_enable_notification(&vdev->vq[i]);

        /* check for work one more time.  otherwise,
           we race from the last time we tried to process
           and when we enable notification */
        if (vdev->vq[i].handle_output(vdev, &vdev->vq[i])) {
            goto again;
        }
    }
}

void virtio_notify_queue(struct virtio_device *vdev, struct virtqueue *vq)
{
    if (!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
        vdev->trans->notify(vdev->trans, vq->vq_num);
    }
}

struct virtqueue *virtio_init_vq(struct virtio_device *vdev,
                                 int vq_num, uint64_t addr, int num,
                                 bool (*callback)(struct virtio_device *,
                                                  struct virtqueue *))
{
    struct virtqueue *vq = &vdev->vq[vq_num];

    vdev->num_vqs = MAX(vq_num + 1, vdev->num_vqs);
    vq->handle_output = callback;
    vq->vq_num = vq_num;
    vring_init(&vq->vring,
               num,
               vdev->trans->map(vdev->trans, addr, vring_size(num, PAGE_SIZE)),
               PAGE_SIZE);

    return vq;
}
                                 
