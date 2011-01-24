/*
 * Wrappers around mutex/cond/thread functions
 *
 * Copyright Red Hat, Inc. 2009
 *
 * Author:
 *  Marcelo Tosatti <mtosatti@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qemu-thread.h"

struct trampoline_data
{
    QemuThread *thread;
    void *(*startfn)(void *);
    void *opaque;
    GStaticMutex lock;
};

static gpointer thread_trampoline(gpointer data)
{
    struct trampoline_data *td = data;
    gpointer retval;

    td->thread->tid = pthread_self();
    g_static_mutex_unlock(&td->lock);

    retval = td->startfn(td->opaque);
    qemu_free(td);

    return retval;
}

void qemu_thread_create(QemuThread *thread,
                        void *(*start_routine)(void*),
                        void *arg)
{
    struct trampoline_data *td = qemu_malloc(sizeof(*td));
    sigset_t set, old;

    td->startfn = start_routine;
    td->opaque = arg;
    td->thread = thread;
    g_static_mutex_init(&td->lock);

    /* on behalf of the new thread */
    g_static_mutex_lock(&td->lock);

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &old);
    thread->thread = g_thread_create(thread_trampoline, td, TRUE, NULL);
    pthread_sigmask(SIG_SETMASK, &old, NULL);

    /* we're transfering ownership of this lock to the thread so we no
     * longer hold it here */

    g_static_mutex_lock(&td->lock);
    /* validate tid */
    g_static_mutex_unlock(&td->lock);

    g_static_mutex_free(&td->lock);
}

void qemu_thread_signal(QemuThread *thread, int sig)
{
    pthread_kill(thread->tid, sig);
}

void qemu_thread_self(QemuThread *thread)
{
    thread->thread = g_thread_self();
    thread->tid = pthread_self();
}

int qemu_thread_equal(QemuThread *thread1, QemuThread *thread2)
{
    return (thread1->thread == thread2->thread);
}

void qemu_thread_exit(void *retval)
{
    g_thread_exit(retval);
}
