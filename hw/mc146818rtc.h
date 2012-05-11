#ifndef MC146818RTC_H
#define MC146818RTC_H

#include "isa.h"
#include "mc146818rtc_regs.h"
#include "qemu/qc-markers.h"

typedef struct RTCState {
    ISADevice dev;
    MemoryRegion io;
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    struct tm current_tm;
    int32_t _property base_year _default(1980);
    qemu_irq irq;
    qemu_irq sqw_irq;
    int _immutable it_shift;
    /* periodic timer */
    QEMUTimer *periodic_timer;
    int64_t next_periodic_time;
    /* second update */
    int64_t next_second_time;
    QEMUTimer _broken *coalesced_timer;
    QEMUTimer *second_timer;
    QEMUTimer *second_timer2;
    uint16_t _broken irq_reinject_on_ack_count;
    uint32_t irq_coalesced _version(2);
    uint32_t period _version(2);
    Notifier clock_reset_notifier;
    LostTickPolicy _property lost_tick_policy _default(LOST_TICK_DISCARD);
    Notifier suspend_notifier;
} RTCState;

ISADevice *rtc_init(ISABus *bus, int base_year, qemu_irq intercept_irq);
void rtc_set_memory(ISADevice *dev, int addr, int val);
void rtc_set_date(ISADevice *dev, const struct tm *tm);

#endif /* !MC146818RTC_H */
