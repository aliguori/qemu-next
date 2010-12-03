/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu-ioh.h"
#include "qlist.h"

/* XXX: fd_read_poll should be suppressed, but an API change is
   necessary in the character devices to suppress fd_can_read(). */
int qemu_set_fd_handler3(void *ioh_record_list,
                         int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    QLIST_HEAD(, IOHandlerRecord) *io_handlers_ptr = ioh_record_list;
    IOHandlerRecord *ioh;

    if (!fd_read && !fd_write) {
        QLIST_FOREACH(ioh, io_handlers_ptr, next) {
            if (ioh->fd == fd) {
                ioh->deleted = 1;
                break;
            }
        }
    } else {
        QLIST_FOREACH(ioh, io_handlers_ptr, next) {
            if (ioh->fd == fd)
                goto found;
        }
        ioh = qemu_mallocz(sizeof(IOHandlerRecord));
        QLIST_INSERT_HEAD(io_handlers_ptr, ioh, next);
    found:
        ioh->fd = fd;
        ioh->fd_read_poll = fd_read_poll;
        ioh->fd_read = fd_read;
        ioh->fd_write = fd_write;
        ioh->opaque = opaque;
        ioh->deleted = 0;
    }
    return 0;
}

/* add entries from ioh record list to fd sets. nfds and fd sets
 * should be cleared/reset by caller if desired. set a particular
 * fdset to NULL to ignore fd events of that type
 */
void qemu_get_fdset2(void *ioh_record_list, int *nfds, fd_set *rfds,
                     fd_set *wfds, fd_set *xfds)
{
    QLIST_HEAD(, IOHandlerRecord) *io_handlers = ioh_record_list;
    IOHandlerRecord *ioh;

    QLIST_FOREACH(ioh, io_handlers, next) {
        if (ioh->deleted)
            continue;
        if ((rfds != NULL && ioh->fd_read) &&
            (!ioh->fd_read_poll ||
             ioh->fd_read_poll(ioh->opaque) != 0)) {
            FD_SET(ioh->fd, rfds);
            if (ioh->fd > *nfds)
                *nfds = ioh->fd;
        }
        if (wfds != NULL && ioh->fd_write) {
            FD_SET(ioh->fd, wfds);
            if (ioh->fd > *nfds)
                *nfds = ioh->fd;
        }
    }
}

/* execute registered handlers for r/w events in the provided fdsets. unset
 * handlers are cleaned up here as well
 */
void qemu_process_fd_handlers2(void *ioh_record_list, const fd_set *rfds,
                               const fd_set *wfds, const fd_set *xfds)
{
    QLIST_HEAD(, IOHandlerRecord) *io_handlers = ioh_record_list;
    IOHandlerRecord *ioh, *pioh;

    QLIST_FOREACH_SAFE(ioh, io_handlers, next, pioh) {
        if (!ioh->deleted && ioh->fd_read && FD_ISSET(ioh->fd, rfds)) {
            ioh->fd_read(ioh->opaque);
        }
        if (!ioh->deleted && ioh->fd_write && FD_ISSET(ioh->fd, wfds)) {
            ioh->fd_write(ioh->opaque);
        }

        /* Do this last in case read/write handlers marked it for deletion */
        if (ioh->deleted) {
            QLIST_REMOVE(ioh, next);
            qemu_free(ioh);
        }
    }
}
