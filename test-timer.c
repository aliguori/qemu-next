#include <glib.h>
#include "qemu-timer.h"
#include "sysemu.h"

int use_icount;
int vm_running;
int64_t qemu_icount;
QEMUClock *rtc_clock;

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb, void *opaque)
{
    return NULL;
}

void qemu_notify_event(void)
{
}

int64_t cpu_get_icount(void)
{
    return 0;
}

bool all_cpu_threads_idle(void)
{
    return false;
}

static int64_t before;
static QEMUTimer *ts;

static void hello_world(void *opaque)
{
    GMainLoop *loop = opaque;
    int64_t after = qemu_get_clock_ns(rt_clock);
    static int count;

    printf("Hello World! after %f seconds\n",
           (double)(after - before) / get_ticks_per_sec());

    if (count++ > 10) {
        g_main_loop_quit(loop);
    } else {
        before = qemu_get_clock_ns(rt_clock);
        qemu_mod_timer(ts, before + get_ticks_per_sec() / 1);
    }
}

int main(int argc, char **argv)
{
    GMainLoop *loop;

    init_clocks();
    init_timer_alarm();

    loop = g_main_loop_new(g_main_context_default(), FALSE);

    ts = qemu_new_timer_ns(rt_clock, hello_world, loop);

    before = qemu_get_clock_ns(rt_clock);
    qemu_mod_timer(ts, before + get_ticks_per_sec());

    g_main_loop_run(loop);

    return 0;
}
