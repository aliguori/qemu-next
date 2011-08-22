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
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef G_HRTIMER_H
#define G_HRTIMER_H 1

#include <glib.h>

#define G_HRTIMER_QUIESCE	((gint64) 0x7FFFFFFFFFFFFFFF)

typedef struct _GHRTimer GHRTimer;

gint64 g_get_monotonic_time_ns (void);

gint64 g_source_get_time_ns    (GSource *source);

GHRTimer *g_hrtimer_new        (void);

gboolean g_hrtimer_pending     (GHRTimer		*timer);

void   g_hrtimer_rearm         (GHRTimer		*timer,
                                gint64			 usec);

void   g_hrtimer_rearm_ns      (GHRTimer		*timer,
                                gint64			 nsec);

guint g_hrtimer_add	       (GHRTimer	       **timer,
				GSourceFunc		 callback,
				gpointer		 user_data);

guint g_hrtimer_add_full       (int			 priority,
				GHRTimer	       **timer,
				GSourceFunc		 callback,
				gpointer		 user_data,
				GDestroyNotify		 notify);

#endif
