#ifndef QEMU_VNF_VIRTIO_NET_H
#define QEMU_VNF_VIRTIO_NET_H

#include "vnf/loop.h"
#include "vnf/virtio.h"

struct virtio_device *vnf_init(struct virtio_transport *trans,
                               uint64_t rx_addr, unsigned rx_num,
                               uint64_t tx_addr, unsigned tx_num,
                               int tap_fd);

#endif
