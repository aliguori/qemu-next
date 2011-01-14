/*
 * Compatibility for qemu-img/qemu-nbd
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qemu-tool.h"
#include "monitor.h"
#include "qemu-timer.h"
#include "qemu-log.h"
#include "sysemu.h"

#include <sys/time.h>

QEMUClock *rtc_clock;

FILE *logfile;
static QLIST_HEAD(, IOHandlerRecord) io_handlers =
    QLIST_HEAD_INITIALIZER(io_handlers);
struct QEMUBH
{
    QEMUBHFunc *cb;
    void *opaque;
};

void qemu_service_io(void)
{
}

Monitor *cur_mon;

int monitor_cur_is_qmp(void)
{
    return 0;
}

void monitor_set_error(Monitor *mon, QError *qerror)
{
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
}

void async_context_push(void)
{
}

void async_context_pop(void)
{
}

int get_async_context_id(void)
{
    return 0;
}

void monitor_protocol_event(MonitorEvent event, QObject *data)
{
}

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;

    bh = qemu_malloc(sizeof(*bh));
    bh->cb = cb;
    bh->opaque = opaque;

    return bh;
}

int qemu_bh_poll(void)
{
    return 0;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    bh->cb(bh->opaque);
}

void qemu_bh_cancel(QEMUBH *bh)
{
}

void qemu_bh_delete(QEMUBH *bh)
{
    qemu_free(bh);
}

/* definitions to implement i/o loop for fd handlers in tools */
int qemu_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    return qemu_set_fd_handler3(&io_handlers, fd, fd_read_poll, fd_read,
                                fd_write, opaque);
}

int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    return qemu_set_fd_handler2(fd, NULL, fd_read, fd_write, opaque);
}

void qemu_get_fdset(int *nfds, fd_set *rfds, fd_set *wfds, fd_set *xfds)
{
    return qemu_get_fdset2(&io_handlers, nfds, rfds, wfds, xfds);
}

void qemu_process_fd_handlers(const fd_set *rfds, const fd_set *wfds,
                              const fd_set *xfds)
{
    return qemu_process_fd_handlers2(&io_handlers, rfds, wfds, xfds);
}

#ifndef _WIN32
static int io_thread_fd = -1;

void qemu_event_increment(void)
{
    return iothread_event_increment(&io_thread_fd);
}

int qemu_event_init(void)
{
    return iothread_event_init(&io_thread_fd);
}
#else
HANDLE qemu_event_handle;

int qemu_event_init(void)
{
    return win32_event_init(&qemu_event_handle);
}

void qemu_event_increment(void)
{
    win32_event_increment(&qemu_event_handle);
}
#endif

void qemu_notify_event(void)
{
    qemu_event_increment ();
}

/*
 * Creates an eventfd that looks like a pipe and has EFD_CLOEXEC set.
 */
int qemu_eventfd(int fds[2])
{
#ifdef CONFIG_EVENTFD
    int ret;

    ret = eventfd(0, 0);
    if (ret >= 0) {
        fds[0] = ret;
        qemu_set_cloexec(ret);
        if ((fds[1] = dup(ret)) == -1) {
            close(ret);
            return -1;
        }
        qemu_set_cloexec(fds[1]);
        return 0;
    }

    if (errno != ENOSYS) {
        return -1;
    }
#endif

    return qemu_pipe(fds);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    return 0;
}

const VMStateInfo vmstate_info_int64;
int use_icount = 0;
int vm_running = 1;
int64_t qemu_icount;

int vmstate_register(DeviceState *dev, int instance_id,
                     const VMStateDescription *vmsd, void *opaque)
{
    return 0;
}
int64_t cpu_get_icount(void) {
    return 0;
}

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    return NULL;
}
