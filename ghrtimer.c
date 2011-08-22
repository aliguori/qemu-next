/*
 * timerfd GSource wrapper
 *
 * Copyright IBM, Corp. 2011
 * Copyright Red Hat, Inc. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "config-host.h"
#include <stdlib.h>
#include "ghrtimer.h"
#include <glib-object.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef CONFIG_TIMERFD
#include <sys/timerfd.h>
#endif

#ifdef CLOCK_MONOTONIC_RAW
#define CLOCK_PREFERRED CLOCK_MONOTONIC_RAW
#else
#define CLOCK_PREFERRED CLOCK_MONOTONIC
#endif

struct _GHRTimer {
    GSource		    source;
    gint64		    deadline;
    GPollFD                 poll;
    char		    pending;
};

#define MIN_TIMER_REARM_NS	250000

#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION <= 26
static inline guint64 muldiv64(guint64 a, guint32 b, guint32 c)
{
    guint64 rl = (a & 0xffffffff) * (guint64)b;
    guint64 rh = (a >> 32) * (guint64)b + (rl >> 32);
    rl &= 0xffffffff;
    return ((rh / c) << 32) | ((((rh % c) << 32) + rl) / c);
}

gint64
g_get_monotonic_time_ns (void)
{
#if defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD_version >= 500000) \
            || defined(__DragonFly__) || defined(__FreeBSD_kernel__) \
            || defined(__OpenBSD__)
    struct timespec ts;
    clock_gettime(CLOCK_PREFERRED, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;

#elif defined _WIN32
    LARGE_INTEGER ti;
    static LARGE_INTEGER freq;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&ti);
    return muldiv64(ti.QuadPart, 1000000000, freq.QuadPart);

#else
#ifdef CONFIG_TIMERFD
#error configuration problem, timerfd uses CLOCK_MONOTONIC
#endif
    GTimeVal tv;
    g_get_current_time (&tv);
    return ((guint64) tv.tv_sec) * 1000000000 + tv.tv_usec * 1000;
#endif
}

gint64
g_source_get_time_ns (GSource *source)
{
    return g_get_monotonic_time_ns ();
}

#else
gint64
g_get_monotonic_time_ns (void)
{
    return g_get_monotonic_time () * 1000;
}

gint64
g_source_get_time_ns (GSource *source)
{
    return g_source_get_time (source) * 1000;
}
#endif

gboolean
g_hrtimer_pending (GHRTimer *timer)
{
    return timer->pending;
}

void
g_hrtimer_rearm (GHRTimer *timer,
		 gint64    us)
{
    return g_hrtimer_rearm_ns (timer, us * 1000);
}

void
g_hrtimer_rearm_ns (GHRTimer *timer,
		    gint64    ns)
{
    gint64 time = g_get_monotonic_time_ns ();
    timer->deadline = ns;
    if (ns < time) {
	timer->pending = TRUE;
	return;
    }

    timer->pending = FALSE;
    if (ns == G_HRTIMER_QUIESCE) {
	return;
    }

    if (ns - time < MIN_TIMER_REARM_NS) {
        timer->deadline = ns = time + MIN_TIMER_REARM_NS;
    }
#ifdef CONFIG_TIMERFD
    if (timer->poll.fd != -1) {
        struct itimerspec new = {
            .it_interval = { 0, 0 },
            .it_value = { ns / 1000000000, ns % 1000000000 }
        };
        timerfd_settime(timer->poll.fd, TFD_TIMER_ABSTIME, &new, NULL);
    }
#endif
}

static gboolean
g_hrtimer_prepare (GSource *source,
                   gint    *timeout)
{
    GHRTimer *timer = (GHRTimer *) source;

    if (timer->deadline == G_HRTIMER_QUIESCE) {
	g_assert (!timer->pending);
	*timeout = -1;
	return FALSE;
    }

    if (timer->poll.fd == -1) {
        gint64 timeout_ns = timer->deadline - g_get_monotonic_time_ns ();
	if (timeout_ns < 0) {
	    *timeout = 0;
            timer->pending = TRUE;
	} else {
            *timeout = timeout_ns / 1000000;
	}
    } else {
	*timeout = -1;
#ifndef CONFIG_TIMERFD
        abort ();
#endif
    }
    return timer->pending;
}

static gboolean
g_hrtimer_check (GSource *source)
{
    GHRTimer *timer = (GHRTimer *) source;

    if (timer->deadline == G_HRTIMER_QUIESCE) {
	g_assert (!timer->pending);
	return FALSE;
    }

    if (timer->poll.fd == -1) {
        timer->pending |= (timer->deadline <= g_source_get_time_ns (source));
    } else {
        long long overrun;
        timer->pending |= (timer->poll.revents & G_IO_IN) != 0;
	if (timer->pending) {
	    if (read (timer->poll.fd, (char *) &overrun, sizeof (overrun))) {
		/* do nothing */
            }
	}
#ifndef CONFIG_TIMERFD
        abort ();
#endif
    }

    return timer->pending;
}

static gboolean
g_hrtimer_dispatch (GSource *source,
                    GSourceFunc  callback,
                    gpointer     user_data)
{
    GHRTimer *timer = (GHRTimer *) source;

    if (!callback) {
        g_warning ("Timer source dispatched without callback\n"
                   "You must call g_source_set_callback().");
        return TRUE;
    }

    timer->pending = FALSE;
    timer->deadline = G_HRTIMER_QUIESCE;
    if (user_data == NULL)
        user_data = timer;
    callback (user_data);
    return TRUE;
}

static void
g_hrtimer_finalize (GSource *source)
{
    GHRTimer *timer = (GHRTimer *) source;

    if (timer->poll.fd != -1) {
        close (timer->poll.fd);
#ifndef CONFIG_TIMERFD
        abort ();
#endif
    }
}

static void
g_hrtimer_closure_callback (gpointer data)
{
    GClosure *closure = data;
    g_closure_invoke (closure, NULL, 0, NULL, NULL);
}

static GSourceFuncs hrtimer_source_funcs = {
  g_hrtimer_prepare,
  g_hrtimer_check,
  g_hrtimer_dispatch,
  g_hrtimer_finalize,
  (GSourceFunc) g_hrtimer_closure_callback,
  (gpointer) g_cclosure_marshal_VOID__VOID
};

GHRTimer *
g_hrtimer_new (void)
{
    GHRTimer *timer;

    timer = (GHRTimer *) g_source_new (&hrtimer_source_funcs,
                                       sizeof (GHRTimer));

#ifdef CONFIG_TIMERFD
    timer->poll.fd = timerfd_create (CLOCK_PREFERRED, 0);
    if (timer->poll.fd != -1) {
        fcntl(timer->poll.fd, F_SETFD, fcntl (timer->poll.fd, F_GETFD) | FD_CLOEXEC);
        fcntl(timer->poll.fd, F_SETFL, fcntl (timer->poll.fd, F_GETFL) | O_NONBLOCK);
        timer->poll.events = G_IO_IN;
        g_source_add_poll (&timer->source, &timer->poll);
    }
#else
    timer->poll.fd = -1;
#endif
    timer->deadline = G_HRTIMER_QUIESCE;
    return timer;
}
guint
g_hrtimer_add (GHRTimer     **timer,
	       GSourceFunc    func,
	       gpointer       user_data)
{
    return g_hrtimer_add_full (G_PRIORITY_DEFAULT, timer, func, user_data, NULL);
}

guint
g_hrtimer_add_full (gint              priority,
		    GHRTimer        **timer,
		    GSourceFunc       func,
		    gpointer          user_data,
		    GDestroyNotify    notify)
{
    GHRTimer *hrtimer;
    guint id;

    hrtimer = g_hrtimer_new ();
    if (priority != G_PRIORITY_DEFAULT)
        g_source_set_priority (&hrtimer->source, priority);

    g_source_set_callback (&hrtimer->source, (GSourceFunc) func,
                           user_data, notify);
    id = g_source_attach (&hrtimer->source, NULL);

    *timer = hrtimer;
    return id;
}

#ifdef MAIN
#include <stdio.h>

static int i = 3;
static GMainLoop *loop;

void
rearm_timer (GHRTimer *timer)
{
    printf (".");
    fflush (stdout);
    g_hrtimer_rearm_ns (timer, g_get_monotonic_time_ns () + 1000000000);
}

void
hrtimer_callback (gpointer user_data)
{
    GHRTimer *timer = user_data;

    if (--i == 0) {
        printf ("\n");
        fflush (stdout);
        g_main_loop_quit (loop);
    } else {
        rearm_timer (timer);
    }
}

int main()
{
    GHRTimer *timer;
    loop = g_main_loop_new (NULL, FALSE);
    g_hrtimer_add (&timer, (GSourceFunc) hrtimer_callback, NULL);
    rearm_timer (timer);
    g_main_loop_run (loop);
    g_source_unref ((GSource *) timer);
}
#endif

