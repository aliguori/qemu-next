#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#define MS_PER_SEC 1000UL
#define US_PER_SEC 1000000UL
#define NS_PER_SEC 1000000000UL

struct clock_impl;

struct clock
{
    struct clock_impl *impl;
};

struct clock *clock_get_instance(void);

uint64_t clock_get_ns(struct clock *c);

uint64_t clock_get_us(struct clock *c);

uint64_t clock_get_ms(struct clock *c);

uint64_t clock_get_sec(struct clock *c);

#endif
