/*
 * virt-proxy - host/guest communication layer
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VIRTPROXY_H
#define VIRTPROXY_H

#include "qemu-common.h"
#include "qemu-queue.h"
#include "qemu-char.h"

typedef struct VPDriver VPDriver;
enum vp_context {
    VP_CTX_CHARDEV, /* in qemu/host, channel is a virtproxy chardev */
    VP_CTX_FD,      /* in guest, channel is an FD */
};
extern QemuOptsList vp_opts;

/* wrappers for s/vp/qemu/ functions we need */
int vp_send_all(int fd, const void *buf, int len1);
int vp_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque);
int vp_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque);
void vp_chr_read(CharDriverState *s, uint8_t *buf, int len);

/* virtproxy interface */
VPDriver *vp_new(enum vp_context ctx, CharDriverState *s, int fd, bool listen);
int vp_set_oforward(VPDriver *drv, int fd, const char *service_id);
int vp_set_iforward(VPDriver *drv, const char *service_id, const char *addr,
                    const char *port, bool ipv6);
int vp_handle_packet_buf(VPDriver *drv, const void *buf, int count);

#endif /* VIRTPROXY_H */
