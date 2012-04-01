#include "newemu/clock.h"
#include "newemu/clock-glib.h"

#include <glib/gthread.h>

static gpointer io_thread_func(gpointer data)
{
    struct clock *clock = data;
    GMainLoop *loop;

    loop = g_main_loop_new(clock->impl->context, FALSE);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_main_context_unref(clock->impl->context);

    return NULL;
}

struct clock *clock_get_instance(void)
{
    static struct clock *global_vm_clock;

    if (!global_vm_clock) {
        g_thread_init(NULL);

        global_vm_clock = g_malloc0(sizeof(struct clock));
        global_vm_clock->impl = g_malloc0(sizeof(struct clock_impl));
        global_vm_clock->impl->context = g_main_context_new();
        global_vm_clock->impl->thread = g_thread_create(io_thread_func,
                                                        global_vm_clock,
                                                        FALSE, NULL);
    }

    return global_vm_clock;
}

uint64_t clock_get_ns(struct clock *c)
{
    return g_get_monotonic_time() * 1000L;
}
