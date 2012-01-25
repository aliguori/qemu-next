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
    qemu_irq irq;
} PITChannelState;

typedef struct PITState {
    ISADevice dev;
    MemoryRegion ioports;
    uint32_t irq;
    uint32_t iobase;
    PITChannelState channels[3];
} PITState;

static inline ISADevice *pit_init(ISABus *bus, int base, int irq)
{
    ISADevice *dev;

    dev = isa_create(bus, "isa-pit");
    qdev_prop_set_uint32(&dev->qdev, "iobase", base);
    qdev_prop_set_uint32(&dev->qdev, "irq", irq);
    qdev_init_nofail(&dev->qdev);

    return dev;
}

void pit_set_gate(PITState *pit, int channel, int val);
int pit_get_gate(PITState *pit, int channel);
int pit_get_initial_count(PITState *pit, int channel);
int pit_get_mode(PITState *pit, int channel);
int pit_get_out(PITState *pit, int channel, int64_t current_time);

void hpet_pit_disable(void);
void hpet_pit_enable(void);

#endif
