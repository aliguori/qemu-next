#ifndef CLOCK_PRIV_H
#define CLOCK_PRIV_H

#include "qemu-common.h"
#include "qemu-timer.h"

struct clock_impl
{
    QEMUClock *clock;
};

#endif
