#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1
#include <glib.h>
#include <pthread.h>

struct QemuThread {
    GThread *thread;
    pthread_t tid;
};

typedef struct QemuThread QemuThread;

void qemu_thread_create(QemuThread *thread,
                       void *(*start_routine)(void*),
                       void *arg);
void qemu_thread_signal(QemuThread *thread, int sig);
void qemu_thread_self(QemuThread *thread);
int qemu_thread_equal(QemuThread *thread1, QemuThread *thread2);
void qemu_thread_exit(void *retval);

#endif
