#ifndef VIRTPROXY_H
#define VIRTPROXY_H

#include "qemu-common.h"
#include "qemu-queue.h"

typedef struct VPDriver VPDriver;

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

/* virtproxy interface */
VPDriver *vp_new(int fd, bool listen);
int vp_set_oforward(VPDriver *drv, int fd, const char *service_id);
int vp_set_iforward(VPDriver *drv, const char *service_id, const char *addr,
                    const char *port, bool ipv6);

#endif /* VIRTPROXY_H */
