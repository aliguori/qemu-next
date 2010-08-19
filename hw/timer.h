#ifndef QEMU_NEW_TIMER_H
#define QEMU_NEW_TIMER_H

#include "qemu-common.h"

#define MS_PER_SEC 1000ULL
#define US_PER_SEC 1000000ULL

typedef struct TimerOperations TimerOperations;
typedef struct Timer Timer;

struct Timer
{
    void (*fn)(struct Timer *timer);
    void *priv;
};

void timer_init(Timer *timer, void (*fn)(Timer *timer));

void timer_cancel(Timer *timer);

void timer_update_rel_ms(Timer *timer, uint64_t ms);

void timer_update_rel_us(Timer *timer, uint64_t us);

#endif
