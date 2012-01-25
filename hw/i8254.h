#ifndef QEMU_I8254_H
#define QEMU_I8254_H

#include "hw.h"
#include "isa.h"
#include "qemu-timer.h"

#define PIT_FREQ 1193182

#define TYPE_PIT "isa-pit"
#define PIT(obj) OBJECT_CHECK(PITState, (obj), TYPE_PIT)

typedef struct PITChannelState {
    int count; /* can be 65536 */
    uint16_t latched_count;
    uint8_t count_latched;
    uint8_t status_latched;
    uint8_t status;
    uint8_t read_state;
    uint8_t write_state;
    uint8_t write_latch;
    uint8_t rw_mode;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */
    int64_t count_load_time;
    /* irq handling */
    int64_t next_transition_time;
    QEMUTimer *irq_timer;
    Pin irq;
} PITChannelState;

typedef struct PITState {
    ISADevice dev;
    MemoryRegion ioports;
    uint32_t iobase;
    PITChannelState channels[3];
} PITState;

void pit_set_gate(PITState *pit, int channel, int val);
int pit_get_gate(PITState *pit, int channel);
int pit_get_initial_count(PITState *pit, int channel);
int pit_get_mode(PITState *pit, int channel);
int pit_get_out(PITState *pit, int channel, int64_t current_time);
void pit_set_iobase(PITState *pit, uint32_t iobase);

void hpet_pit_disable(void);
void hpet_pit_enable(void);

static inline ISADevice *pit_init(ISABus *bus, int base, int irqno)
{
    PITState *pit;
    qemu_irq irq;

    pit = PIT(object_new("isa-pit"));
    pit_set_iobase(pit, base);

    isa_init_irq(ISA_DEVICE(pit), &irq, irqno);
    pin_connect_qemu_irq(&pit->channels[0].irq, irq);

    qdev_init_nofail(DEVICE(pit));

    return ISA_DEVICE(pit);
}

#endif
