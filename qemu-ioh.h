#ifndef QEMU_IOH_H
#define QEMU_IOH_H

#include "qemu-common.h"
#include "qlist.h"

/* common i/o loop definitions */

typedef struct IOHandlerRecord {
    int fd;
    IOCanReadHandler *fd_read_poll;
    IOHandler *fd_read;
    IOHandler *fd_write;
    int deleted;
    void *opaque;
    /* temporary data */
    struct pollfd *ufd;
    QLIST_ENTRY(IOHandlerRecord) next;
} IOHandlerRecord;

/* XXX: fd_read_poll should be suppressed, but an API change is
   necessary in the character devices to suppress fd_can_read(). */
int qemu_set_fd_handler3(void *io_handlers_ptr,
                         int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque);
void qemu_get_fdset2(void *ioh_record_list, int *nfds, fd_set *rfds,
                     fd_set *wfds, fd_set *xfds);
void qemu_process_fd_handlers2(void *ioh_record_list, const fd_set *rfds,
                               const fd_set *wfds, const fd_set *xfds);


#ifndef _WIN32
void iothread_event_increment(int *io_thread_fd);
int iothread_event_init(int *io_thread_fd);
#else
int win32_event_init(HANDLE *qemu_event_handle);
void win32_event_increment(HANDLE *qemu_event_handle);
#endif

#endif
