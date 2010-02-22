#ifndef QEMU_VNF_LOOP_H
#define QEMU_VNF_LOOP_H

#define IO_CALLBACK_READ   1
#define IO_CALLBACK_WRITE  2

struct io_callback;

struct io_callback
{
    void (*callback)(struct io_callback *obj);
};

void io_callback_add(int fd, int events, struct io_callback *cb);
void io_callback_remove(int fd, int events);

void idle_callback_add(struct io_callback *cb);
void idle_callback_remove(struct io_callback *cb);

void main_loop_run_once(int timeout);

#endif
