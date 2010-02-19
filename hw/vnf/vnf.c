#include "virtio-net.h"

struct vnf_device
{
    struct virtio_transport trans;
    struct virtio_device *vdev;

    int notify_fd;
    struct io_callback notify_cb;
};

static uint64_t eventfd_read(int fd)
{
    ssize_t len;
    uint64_t count;

    do {
        len = read(fd, &count, sizeof(count));
    } while (ret == -1 && errno == EINTR);
    assert(len == sizeof(uint64_t));

    return count;
}

static void vnf_notify_cb(struct io_callback *cb)
{
    struct vnf_device *vnf = container_of(struct vnf_device,
                                          notify_cb, cb);
    eventfd_read(vnf->notify_fd);
    virtio_kick(vnf->vdev);
}

static void virtio_hack_notify(struct virtio_device *vdev,
                               struct virtqueue *vq)
{
    struct vnf_device *vnf = container_of(struct vnf_device,
                                          trans, vdev->trans);
    /* need to do kvm_ioctl to inject irq using msix vector mapping */
}

static void *vnf_map(struct virtio_device *vdev, uint64_t addr, uint32_t size)
{
    struct vnf_device *vnf = container_of(struct vnf_device,
                                          trans, vdev->trans);
    /* need to do qemu_ram_ptr magic */
}

static void *vnf_thread(void *opaque)
{
    struct vnf_device *vnf = opaque;

    while (true) {
        pthread_test_cancel();
        main_loop_run_once(5000);
    }

    return NULL;
}

void vnf_init(int notify_fd,
              uint64_t rx_addr, unsigned rx_num,
              uint64_t tx_addr, unsigned tx_num,
              int tap_fd)
{
    struct vnf_device *vnf;

    vnf = malloc(sizeof(*vnf));
    memset(vnf, 0, sizeof(*vnf));

    vnf->trans.map = vnf_map;
    vnf->trans.notify = vnf_notify;
    vnf->notify_fd = notify_fd;
    vnf->notify_cb.callback = vnf_notify_cb;
    vnf->vdev = virtio_net_init(&vnf->trans,
                                rx_addr, rx_num,
                                tx_addr, tx_num,
                                tap_fd);

    io_callback_add(notify_fd, IO_CALLBACK_READ, &vnc->notify_cb);

    /* mask signals */
    pthread_create(&tid, NULL, vnf_thread, vnf);
}
