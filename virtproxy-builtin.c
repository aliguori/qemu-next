/*
 * virt-proxy - host/guest communication layer builtin definitions
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Adam Litke        <aglitke@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* the following are functions we define in terms of qemu when linked
 * against qemu/vl.c. these will be added on an as-needed basis
 */

#include "qemu-char.h"
#include "virtproxy.h"

int vp_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    return qemu_set_fd_handler(fd, fd_read, fd_write, opaque);
}


