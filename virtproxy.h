#ifndef VIRTPROXY_H
#define VIRTPROXY_H

#include "qemu-common.h"
#include "qemu-queue.h"

typedef struct {
    QLIST_HEAD(, VPOForward) oforwards;
    QLIST_HEAD(, VPConn) conns;
} VPDriver;

#endif /* VIRTPROXY_H */
