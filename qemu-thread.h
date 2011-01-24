#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1
#include <glib.h>
#include <pthread.h>

struct QemuCond {
    GCond *cond;
};

struct QemuThread {
    GThread *thread;
    pthread_t tid;
};

typedef struct QemuCond QemuCond;
typedef struct QemuThread QemuThread;

void qemu_cond_init(QemuCond *cond);
void qemu_cond_destroy(QemuCond *cond);
void qemu_cond_signal(QemuCond *cond);
void qemu_cond_broadcast(QemuCond *cond);
void qemu_cond_wait(QemuCond *cond, GMutex *mutex);
int qemu_cond_timedwait(QemuCond *cond, GMutex *mutex, uint64_t msecs);

void qemu_thread_create(QemuThread *thread,
                       void *(*start_routine)(void*),
                       void *arg);
void qemu_thread_signal(QemuThread *thread, int sig);
void qemu_thread_self(QemuThread *thread);
int qemu_thread_equal(QemuThread *thread1, QemuThread *thread2);
void qemu_thread_exit(void *retval);

#endif
