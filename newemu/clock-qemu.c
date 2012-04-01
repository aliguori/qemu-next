#include "newemu/clock.h"

#include "newemu/clock-qemu.h"

struct clock *clock_get_instance(void)
{
    static struct clock *global_vm_clock;

    if (!global_vm_clock) {
        global_vm_clock = g_malloc0(sizeof(struct clock));
        global_vm_clock->impl = g_malloc0(sizeof(struct clock_impl));
        global_vm_clock->impl->clock = vm_clock;
    }

    return global_vm_clock;
}

uint64_t clock_get_ns(struct clock *c)
{
    return qemu_get_clock_ns(c->impl->clock);
}
