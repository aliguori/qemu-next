#include "newemu/clock.h"

#include "newemu/clock-glib.h"

struct clock *clock_get_instance(void)
{
    static struct clock *global_vm_clock;

    if (!global_vm_clock) {
        global_vm_clock = g_malloc0(sizeof(struct clock));
        global_vm_clock->impl = g_malloc0(sizeof(struct clock_impl));
    }

    return global_vm_clock;
}

uint64_t clock_get_ns(struct clock *c)
{
    return g_get_monotonic_time() * 1000L;
}
