#ifndef QEMU_TIMER_H
#define QEMU_TIMER_H

#include "qemu-common.h"
#include "notify.h"
#include <time.h>
#include <sys/time.h>
#include "ghrtimer.h"

/* timers */

#define SCALE_MS 1000000
#define SCALE_US 1000
#define SCALE_NS 1

typedef struct QEMUClock QEMUClock;
typedef void QEMUTimerCB(void *opaque);

/* The real time clock should be used only for stuff which does not
   change the virtual machine state, as it is run even if the virtual
   machine is stopped. The real time clock has a frequency of 1000
   Hz. */
extern QEMUClock *rt_clock;

/* The virtual clock is only run during the emulation. It is stopped
   when the virtual machine is stopped. Virtual timers use a high
   precision clock, usually cpu cycles (use ticks_per_sec). */
extern QEMUClock *vm_clock;

/* The host clock should be use for device models that emulate accurate
   real time sources. It will continue to run when the virtual machine
   is suspended, and it will reflect system time changes the host may
   undergo (e.g. due to NTP). The host clock has the same precision as
   the virtual clock. */
extern QEMUClock *host_clock;

int64_t qemu_get_clock_ns(QEMUClock *clock);
void qemu_clock_enable(QEMUClock *clock, int enabled);
void qemu_clock_warp(QEMUClock *clock);

void qemu_register_clock_reset_notifier(QEMUClock *clock, Notifier *notifier);
void qemu_unregister_clock_reset_notifier(QEMUClock *clock,
                                          Notifier *notifier);

QEMUTimer *qemu_new_timer(QEMUClock *clock, int scale,
                          QEMUTimerCB *cb, void *opaque);
void qemu_free_timer(QEMUTimer *ts);
void qemu_del_timer(QEMUTimer *ts);
void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time);
int qemu_timer_pending(QEMUTimer *ts);
int qemu_timer_expired(QEMUTimer *timer_head, int64_t current_time);

void qemu_run_all_timers(void);
int qemu_alarm_pending(void);
int64_t qemu_next_icount_deadline(void);
void configure_icount(const char *option);
int qemu_calculate_timeout(void);
void init_clocks(void);
int init_timer_alarm(void);
void quit_timers(void);

int64_t cpu_get_ticks(void);
void cpu_enable_ticks(void);
void cpu_disable_ticks(void);

static inline QEMUTimer *qemu_new_timer_ns(QEMUClock *clock, QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_NS, cb, opaque);
}

static inline QEMUTimer *qemu_new_timer_ms(QEMUClock *clock, QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_MS, cb, opaque);
}

static inline int64_t qemu_get_clock_ms(QEMUClock *clock)
{
    return qemu_get_clock_ns(clock) / SCALE_MS;
}

static inline int64_t get_ticks_per_sec(void)
{
    return 1000000000LL;
}

/* real time host monotonic timer */
static inline int64_t get_clock_realtime(void)
{
    return g_get_monotonic_time_ns();
}

/* Warning: don't insert tracepoints into these functions, they are
   also used by simpletrace backend and tracepoints would cause
   an infinite recursion! */
static inline int64_t get_clock(void)
{
    return g_get_monotonic_time_ns();
}

void qemu_get_timer(QEMUFile *f, QEMUTimer *ts);
void qemu_put_timer(QEMUFile *f, QEMUTimer *ts);

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

ptimer_state *ptimer_init(QEMUBH *bh);
void ptimer_set_period(ptimer_state *s, int64_t period);
void ptimer_set_freq(ptimer_state *s, uint32_t freq);
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload);
uint64_t ptimer_get_count(ptimer_state *s);
void ptimer_set_count(ptimer_state *s, uint64_t count);
void ptimer_run(ptimer_state *s, int oneshot);
void ptimer_stop(ptimer_state *s);

/* icount */
int64_t qemu_icount_round(int64_t count);
extern int64_t qemu_icount;
extern int use_icount;
extern int icount_time_shift;
extern int64_t qemu_icount_bias;
int64_t cpu_get_icount(void);

#ifdef NEED_CPU_H
/* Deterministic execution requires that IO only be performed on the last
   instruction of a TB so that interrupts take effect immediately.  */
static inline int can_do_io(CPUState *env)
{
    if (!use_icount)
        return 1;

    /* If not executing code then assume we are ok.  */
    if (!env->current_tb)
        return 1;

    return env->can_do_io != 0;
}
#endif

#ifdef CONFIG_PROFILER
static inline int64_t profile_getclock(void)
{
    return g_get_monotonic_time_ns();
}

extern int64_t qemu_time, qemu_time_start;
extern int64_t tlb_flush_time;
extern int64_t dev_time;
#endif

#endif
