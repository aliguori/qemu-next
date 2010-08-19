/* THIS CODE SHOULD BE GENERATED SO DON'T DO ANYTHING CLEVER HERE */

#include "serial-marshal.h"

static void marshal_SerialFIFO(Marshaller *m, SerialFIFO *s, const char *name)
{
    marshal_start_struct(m, "SerialFIFO", name);

    marshal_start_array(m, "data");
    {
        int i;
        for (i = 0; i < UART_FIFO_LENGTH; i++) {
            marshal_uint8(m, &s->data[i], NULL);
        }
    }
    marshal_end_array(m);

    marshal_uint8(m, &s->count, "count");
    marshal_uint8(m, &s->itl, "itl");
    marshal_uint8(m, &s->tail, "tail");
    marshal_uint8(m, &s->head, "head");

    marshal_end_struct(m);
}

void marshal_SerialState(Marshaller *m, SerialState *s, const char *name)
{
    marshal_start_struct(m, "SerialState", name);

    marshal_DeviceState(m, &s->qdev, "qdev");
    marshal_qemu_irq(m, &s->gpio_out[0], "gpio_out[0]");
    marshal_SerialInterface(m, s->sif, "sif");
    marshal_uint16(m, &s->divider, "divider");
    marshal_uint8(m, &s->rbr, "rbr");
    marshal_uint8(m, &s->thr, "thr");
    marshal_uint8(m, &s->tsr, "tsr");
    marshal_uint8(m, &s->ier, "ier");
    marshal_uint8(m, &s->iir, "iir");
    marshal_uint8(m, &s->lcr, "lcr");
    marshal_uint8(m, &s->mcr, "mcr");
    marshal_uint8(m, &s->lsr, "lsr");
    marshal_uint8(m, &s->msr, "msr");
    marshal_uint8(m, &s->scr, "scr");
    marshal_uint8(m, &s->fcr, "fcr");
    marshal_int32(m, &s->thr_ipending, "thr_ipending");
    marshal_int32(m, &s->timeout_ipending, "timeout_ipending");
    marshal_int32(m, &s->last_break_enable, "last_break_enable");
    marshal_uint32(m, &s->baudbase, "baudbase");
    marshal_int32(m, &s->tsr_retry, "tsr_retry");
    marshal_SerialFIFO(m, &s->recv_fifo, "recv_fifo");
    marshal_SerialFIFO(m, &s->xmit_fifo, "xmit_fifo");
    marshal_Timer(m, &s->fifo_timeout_timer, "fifo_timeout_timer");
    marshal_Timer(m, &s->transmit_timer, "transmit_timer");
    marshal_Timer(m, &s->modem_status_poll, "modem_status_poll");
    marshal_uint64(m, &s->char_transmit_time_us, "char_transmit_time_us");
    marshal_int32(m, &s->poll_msl, "poll_msl");
    marshal_Notifier(m, &s->break_notifier, "break_notifier");
    marshal_Notifier(m, &s->read_notifier, "read_notifier");
    marshal_Notifier(m, &s->write_notifier, "write_notifier");

    marshal_end_struct(m);
}
