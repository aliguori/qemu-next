#include "vnf/loop.h"
#include "vnf/util.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/select.h>

#define MAX_IOCALLBACK_SLOTS 10

struct io_callback_record
{
    int fd;
    struct io_callback *rdcb;
    struct io_callback *wrcb;
};

static int num_io_callback_slots;
static struct io_callback_record io_callback_slots[MAX_IOCALLBACK_SLOTS];

static int num_idle_callback_slots;
static struct io_callback *idle_callback_slots[MAX_IOCALLBACK_SLOTS];

void io_callback_add(int fd, int events, struct io_callback *cb)
{
    int i;

    for (i = 0; i < num_io_callback_slots; i++) {
        if (io_callback_slots[i].fd == fd) {
            break;
        }
    }

    if (i == num_io_callback_slots) {
        num_io_callback_slots++;
        assert(num_io_callback_slots < MAX_IOCALLBACK_SLOTS);
    }

    if ((events & IO_CALLBACK_READ)) {
        io_callback_slots[i].rdcb = cb;
    }
    if ((events & IO_CALLBACK_WRITE)) {
        io_callback_slots[i].wrcb = cb;
    }
}

void io_callback_remove(int fd, int events)
{
    io_callback_add(fd, events, NULL);
}

void idle_callback_add(struct io_callback *cb)
{
    int i;

    for (i = 0; i < num_idle_callback_slots; i++) {
        if (idle_callback_slots[i] == NULL ||
            idle_callback_slots[i] == cb) {
            break;
        }
    }

    if (i == num_idle_callback_slots) {
        num_idle_callback_slots++;
        assert(num_idle_callback_slots < MAX_IOCALLBACK_SLOTS);
    }

    idle_callback_slots[i] = cb;
}

void idle_callback_remove(struct io_callback *cb)
{
    int i;

    for (i = 0; i < num_idle_callback_slots; i++) {
        if (idle_callback_slots[i] == cb) {
            idle_callback_slots[i] = NULL;
            break;
        }
    }
}

void main_loop_run_once(int timeout)
{
    fd_set rdfds, wrfds;
    int max_fd = -1;
    int i, ret;
    struct timeval tv, *ptv;

    FD_ZERO(&rdfds);
    FD_ZERO(&wrfds);

    for (i = 0; i < num_io_callback_slots; i++) {
        if (io_callback_slots[i].rdcb) {
            max_fd = MAX(max_fd, io_callback_slots[i].fd);
            FD_SET(io_callback_slots[i].fd, &rdfds);
        }
        if (io_callback_slots[i].wrcb) {
            max_fd = MAX(max_fd, io_callback_slots[i].fd);
            FD_SET(io_callback_slots[i].fd, &wrfds);
        }
    }

    if (timeout >= 0) {
        ptv = &tv;
        tv.tv_usec = (timeout % 1000) * 1000;
        tv.tv_sec = timeout / 1000;
    } else {
        ptv = NULL;
    }

    ret = select(max_fd, &rdfds, &wrfds, NULL, ptv);
    assert(ret != -1);

    for (i = 0; i < num_io_callback_slots; i++) {
        if (io_callback_slots[i].rdcb &&
            FD_ISSET(io_callback_slots[i].fd, &rdfds)) {
            io_callback_slots[i].rdcb->callback(io_callback_slots[i].rdcb);
        }
        if (io_callback_slots[i].wrcb &&
            FD_ISSET(io_callback_slots[i].fd, &wrfds)) {
            io_callback_slots[i].wrcb->callback(io_callback_slots[i].wrcb);
        }
    }

    for (i = 0; i < num_idle_callback_slots; i++) {
        if (idle_callback_slots[i]) {
            idle_callback_slots[i]->callback(idle_callback_slots[i]);
        }
    }
}
