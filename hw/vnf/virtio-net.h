#ifndef QEMU_VNF_VIRTIO_NET_H
#define QEMU_VNF_VIRTIO_NET_H

#include "vnf/loop.h"

struct virtio_device *virtio_net_init(struct virtio_transport_ops *trans,
                                      uint64_t rx_addr, unsigned rx_num,
                                      uint64_t tx_addr, unsigned tx_num,
                                      int tap_fd);

#endif
