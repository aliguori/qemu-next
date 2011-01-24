#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1
#include <glib.h>
#include <pthread.h>

/**
 * Light wrapper that sets signal mask appropriately for a non-I/O thread
 */
GThread *q_thread_create_nosignal(GThreadFunc func,
                                  gpointer data,
                                  gboolean joinable,
                                  GError **error);

/**
 * Signal Threads
 *
 * Signal threads are non-portable types of threads that can be signaled
 * directly.  This is an interface that should disappear but requires that an
 * appropriate abstraction be made.  As of today, both TCG and KVM only support
 * being interrupted via a signal so for platforms that don't support this,
 * some other provisions must be made.
 *
 * Please do not use this interface in new code.  Just use GThreads directly.
 */
struct QemuSThread {
    GThread *thread;
    pthread_t tid;
};

typedef struct QemuSThread QemuSThread;

void qemu_sthread_create(QemuSThread *thread,
                         void *(*start_routine)(void*),
                         void *arg);
void qemu_sthread_signal(QemuSThread *thread, int sig);
void qemu_sthread_self(QemuSThread *thread);
int qemu_sthread_equal(QemuSThread *thread1, QemuSThread *thread2);
void qemu_sthread_exit(void *retval);

#endif
