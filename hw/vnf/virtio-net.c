#include "vnf/virtio.h"
#include "vnf/loop.h"
#include "vnf/util.h"

#include <linux/virtio_net.h>

#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>

#define MAX_PACKET_SIZE (sizeof(struct virtio_net_hdr_mrg_rxbuf) + (64 * 1024))

struct virtio_net_device
{
    struct virtio_device vdev;

    struct virtqueue *rx_vq;
    struct virtqueue *tx_vq;

    int tap_fd;
    struct io_callback tap_read_cb;
    struct io_callback tap_write_cb;
};

/* virtio-net I/O functions */
static bool virtio_net_try_to_receive(struct virtio_net_device *dev)
{
    struct virtqueue *vq = dev->rx_vq;
    bool did_work = false;

    while (!virtqueue_empty(vq)) {
        size_t data_size = 0;
        int last_avail, i;
        unsigned heads[1024];
        uint32_t in_sizes[1024];
        int num_heads = 0;
        ssize_t size;
        struct iovec in_sg[1024];
        unsigned in_offset = 0;

        /* Save ring index so we can reset on failure */
        last_avail = vq->last_avail;

        /* Get the next packet buffer */
        while (data_size < MAX_PACKET_SIZE) {
            struct iovec out_sg[1];
            unsigned out_num = sizeof(out_num);
            unsigned in_num = sizeof(in_num) - in_offset;
            unsigned head;

            head = virtio_next_avail(&dev->vdev, vq,
                                     out_sg, &in_sg[in_offset],
                                     &out_num, &in_num);
            if (head == vq->vring.num) {
                break;
            }

            in_offset += in_num;
            in_sizes[num_heads] = iovec_length(in_sg, in_num);
            data_size += in_sizes[num_heads];
            heads[num_heads] = head;

            num_heads++;
        }

        if (data_size < MAX_PACKET_SIZE) {
            vq->last_avail = last_avail;
            break;
        }

        /* Try to receive from tap into packet buffer */
        size = read(dev->tap_fd, in_sg, in_offset);
        if (size == -1 && errno == EAGAIN) {
            vq->last_avail = last_avail;
            /* FIXME: we should disable ring notification */
            break;
        }

        /* Add packet buffer to used queue */
        vq->last_avail = last_avail;
        for (i = 0; i < num_heads && data_size > 0; i++) {
            size_t len = MIN(data_size, in_sizes[i]);
            in_sizes[i] = len;
            vq->last_avail++;
            data_size -= len;
        }

        virtqueue_add_useds(vq, i, heads, in_sizes);
        did_work = true;
    }

    return did_work;
}

static bool virtio_net_try_to_transmit(struct virtio_net_device *dev)
{
    bool did_work;
    struct virtqueue *vq = dev->tx_vq;

    did_work = false;
    while (!virtqueue_empty(vq)) {
        ssize_t len;
        int last_avail;
        unsigned head;
        struct iovec out_sg[1024];
        struct iovec in_sg[1];
        unsigned out_num = sizeof(out_sg);
        unsigned in_num = sizeof(in_num);

        /* Save ring index so we can reset on failure */
        last_avail = vq->last_avail;

        /* Get the next packet buffer */
        head = virtio_next_avail(&dev->vdev, vq,
                                 out_sg, in_sg,
                                 &out_num, &in_num);
        if (head == vq->vring.num) {
            break;
        }

        /* Try to send the packet buffer to tap */
        len = writev(dev->tap_fd, out_sg, out_num);
        if (len == -1 && errno == EAGAIN) {
            vq->last_avail = last_avail;
            io_callback_add(dev->tap_fd, IO_CALLBACK_WRITE, &dev->tap_write_cb);
            /* FIXME: we should disable ring notification */
            break;
        }

        /* Add packet buffer to used queue */
        virtqueue_add_used(vq, head, len);
        did_work = true;
    }

    return did_work;
}

/* virtio-net callback handling */
static void virtio_net_tap_read_cb(struct io_callback *cb)
{
    struct virtio_net_device *dev = container_of(struct virtio_net_device,
                                                 tap_read_cb, cb);

    if (virtio_net_try_to_receive(dev)) {
        virtio_notify(&dev->vdev, dev->rx_vq);
    }
}

static void virtio_net_tap_write_cb(struct io_callback *cb)
{
    struct virtio_net_device *dev = container_of(struct virtio_net_device,
                                                 tap_read_cb, cb);

    io_callback_remove(dev->tap_fd, IO_CALLBACK_WRITE);
    if (virtio_net_try_to_transmit(dev)) {
        virtio_notify(&dev->vdev, dev->tx_vq);
    }
}

static bool virtio_net_receive_cb(struct virtio_device *vdev,
                                  struct virtqueue *vq)
{
    struct virtio_net_device *dev = container_of(struct virtio_net_device,
                                                 vdev, vdev);
    io_callback_add(dev->tap_fd, IO_CALLBACK_READ, &dev->tap_read_cb);
    return virtio_net_try_to_receive(dev);
}

static bool virtio_net_transmit_cb(struct virtio_device *vdev,
                                   struct virtqueue *vq)
{
    struct virtio_net_device *dev = container_of(struct virtio_net_device,
                                                 vdev, vdev);
    return virtio_net_try_to_transmit(dev);
}

struct virtio_device *virtio_net_init(struct virtio_transport_ops *trans,
                                      uint64_t rx_addr, unsigned rx_num,
                                      uint64_t tx_addr, unsigned tx_num,
                                      int tap_fd)
{
    struct virtio_net_device *dev;

    dev = malloc(sizeof(*dev));
    memset(dev, 0, sizeof(*dev));

    dev->vdev.trans = trans;
    dev->rx_vq = virtio_init_vq(&dev->vdev, 0, rx_addr, rx_num,
                                virtio_net_receive_cb);
    dev->tx_vq = virtio_init_vq(&dev->vdev, 1, tx_addr, tx_num,
                                virtio_net_transmit_cb);

    dev->tap_fd = tap_fd;
    dev->tap_read_cb.callback = virtio_net_tap_read_cb;
    dev->tap_write_cb.callback = virtio_net_tap_write_cb;
    io_callback_add(dev->tap_fd, IO_CALLBACK_READ, &dev->tap_read_cb);

    return &dev->vdev;
}
