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

void qemu_mutex_init(QemuMutex *mutex)
{
    g_static_mutex_init(&mutex->lock);
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    g_static_mutex_free(&mutex->lock);
}

void qemu_mutex_lock(QemuMutex *mutex)
{
    g_static_mutex_lock(&mutex->lock);
}

int qemu_mutex_trylock(QemuMutex *mutex)
{
    return g_static_mutex_trylock(&mutex->lock);
}

void qemu_mutex_unlock(QemuMutex *mutex)
{
    g_static_mutex_unlock(&mutex->lock);
}

void qemu_cond_init(QemuCond *cond)
{
    cond->cond = g_cond_new();
}

void qemu_cond_destroy(QemuCond *cond)
{
    g_cond_free(cond->cond);
}

void qemu_cond_signal(QemuCond *cond)
{
    g_cond_signal(cond->cond);
}

void qemu_cond_broadcast(QemuCond *cond)
{
    g_cond_broadcast(cond->cond);
}

void qemu_cond_wait(QemuCond *cond, QemuMutex *mutex)
{
    g_cond_wait(cond->cond, g_static_mutex_get_mutex(&mutex->lock));
}

int qemu_cond_timedwait(QemuCond *cond, QemuMutex *mutex, uint64_t msecs)
{
    GTimeVal abs_time;

    assert(cond->cond != NULL);

    g_get_current_time(&abs_time);
    g_time_val_add(&abs_time, msecs * 1000); /* MSEC to USEC */

    return g_cond_timed_wait(cond->cond,
                             g_static_mutex_get_mutex(&mutex->lock), &abs_time);
}

struct trampoline_data
{
    QemuThread *thread;
    void *(*startfn)(void *);
    void *opaque;
    QemuMutex lock;
};

static gpointer thread_trampoline(gpointer data)
{
    struct trampoline_data *td = data;
    gpointer retval;

    td->thread->tid = pthread_self();
    qemu_mutex_unlock(&td->lock);

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
    qemu_mutex_init(&td->lock);

    /* on behalf of the new thread */
    qemu_mutex_lock(&td->lock);

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &old);
    thread->thread = g_thread_create(thread_trampoline, td, TRUE, NULL);
    pthread_sigmask(SIG_SETMASK, &old, NULL);

    /* we're transfering ownership of this lock to the thread so we no
     * longer hold it here */

    qemu_mutex_lock(&td->lock);
    /* validate tid */
    qemu_mutex_unlock(&td->lock);

    qemu_mutex_destroy(&td->lock);
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
