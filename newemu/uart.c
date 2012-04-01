/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "newemu/uart.h"

#include <string.h>
#include <stdio.h>

//#define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "serial: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
do {} while (0)
#endif

static void _fifo_clear(struct uart *s, int fifo)
{
    struct uart_fifo *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;
    memset(f->data, 0, UART_FIFO_LENGTH);
    f->count = 0;
    f->head = 0;
    f->tail = 0;
}

static int _fifo_put(struct uart *s, int fifo, uint8_t chr)
{
    struct uart_fifo *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;

    /* Receive overruns do not overwrite FIFO contents. */
    if (fifo == XMIT_FIFO || f->count < UART_FIFO_LENGTH) {

        f->data[f->head++] = chr;

        if (f->head == UART_FIFO_LENGTH) {
            f->head = 0;
        }
    }

    if (f->count < UART_FIFO_LENGTH) {
        f->count++;
    } else if (fifo == RECV_FIFO) {
        s->lsr |= UART_LSR_OE;
    }

    return 1;
}

static uint8_t _fifo_get(struct uart *s, int fifo)
{
    struct uart_fifo *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;
    uint8_t c;

    if (f->count == 0) {
        return 0;
    }

    c = f->data[f->tail++];
    if (f->tail == UART_FIFO_LENGTH) {
        f->tail = 0;
    }
    f->count--;

    return c;
}

static void _uart_update_irq(struct uart *s)
{
    uint8_t tmp_iir = UART_IIR_NO_INT;

    if ((s->ier & UART_IER_RLSI) && (s->lsr & UART_LSR_INT_ANY)) {
        tmp_iir = UART_IIR_RLSI;
    } else if ((s->ier & UART_IER_RDI) && s->timeout_ipending) {
        /* Note that(s->ier & UART_IER_RDI) can mask this interrupt,
         * this is not in the specification but is observed on existing
         * hardware.  */
        tmp_iir = UART_IIR_CTI;
    } else if ((s->ier & UART_IER_RDI) && (s->lsr & UART_LSR_DR) &&
               (!(s->fcr & UART_FCR_FE) ||
                s->recv_fifo.count >= s->recv_fifo.itl)) {
        tmp_iir = UART_IIR_RDI;
    } else if ((s->ier & UART_IER_THRI) && s->thr_ipending) {
        tmp_iir = UART_IIR_THRI;
    } else if ((s->ier & UART_IER_MSI) && (s->msr & UART_MSR_ANY_DELTA)) {
        tmp_iir = UART_IIR_MSI;
    }

    s->iir = tmp_iir | (s->iir & 0xF0);

    if (tmp_iir != UART_IIR_NO_INT) {
        pin_raise(&s->irq);
    } else {
        pin_lower(&s->irq);
    }
}

static void _uart_update_parameters(struct uart *s)
{
    int speed, parity, data_bits, stop_bits, frame_size;

    if (s->divider == 0) {
        return;
    }

    /* Start bit. */
    frame_size = 1;
    if (s->lcr & 0x08) {
        /* Parity bit. */
        frame_size++;
        if (s->lcr & 0x10) {
            parity = 'E';
        } else {
            parity = 'O';
        }
    } else {
        parity = 'N';
    }
    if (s->lcr & 0x04) {
        stop_bits = 2;
    } else {
        stop_bits = 1;
    }

    data_bits = (s->lcr & 0x03) + 5;
    frame_size += data_bits + stop_bits;
    speed = s->baudbase / s->divider;
    s->char_transmit_time = (NS_PER_SEC / speed) * frame_size;
    sif_set_params(s->sif, speed, parity, data_bits, stop_bits);

    DPRINTF("speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
}

static void _uart_update_msl(struct uart *s)
{
    uint8_t omsr;
    int flags;

    timer_cancel(&s->modem_status_poll);

    flags = sif_get_tiocm(s->sif);
    if (flags == -ENOTSUP) {
        s->poll_msl = -1;
        return;
    }

    omsr = s->msr;

    if ((flags & SIF_TIOCM_CTS)) {
        s->msr |= UART_MSR_CTS;
    } else {
        s->msr &= ~UART_MSR_CTS;
    }
    if ((flags & SIF_TIOCM_DSR)) {
        s->msr |= UART_MSR_DSR;
    } else {
        s->msr &= ~UART_MSR_DSR;
    }
    if ((flags & SIF_TIOCM_CAR)) {
        s->msr |= UART_MSR_DCD;
    } else {
        s->msr &= ~UART_MSR_DCD;
    }
    if ((flags & SIF_TIOCM_RI)) {
        s->msr |= UART_MSR_RI;
    } else {
        s->msr &= ~UART_MSR_RI;
    }

    if (s->msr != omsr) {
         /* Set delta bits */
         s->msr = s->msr | ((s->msr >> 4) ^ (omsr >> 4));
         /* UART_MSR_TERI only if change was from 1 -> 0 */
         if ((s->msr & UART_MSR_TERI) && !(omsr & UART_MSR_RI)) {
             s->msr &= ~UART_MSR_TERI;
         }
         _uart_update_irq(s);
    }

    /* The real 16550A apparently has a 250ns response latency to line status
       changes. We'll be lazy and poll only every 10ms, and only poll it at all
       if MSI interrupts are turned on */
    if (s->poll_msl) {
        timer_set_deadline_rel_ms(&s->modem_status_poll, 10);
    }
}

static void uart_update_msl_cb(struct timer *t)
{
    struct uart *s = container_of(t, struct uart, modem_status_poll);

    g_mutex_lock(s->lock);
    _uart_update_msl(s);
    g_mutex_unlock(s->lock);
}

static void _uart_xmit(struct uart *s)
{
    uint64_t new_xmit_ts = clock_get_ns(s->clock);

    if (s->tsr_retry <= 0) {
        if (s->fcr & UART_FCR_FE) {
            s->tsr = _fifo_get(s,XMIT_FIFO);
            if (!s->xmit_fifo.count) {
                s->lsr |= UART_LSR_THRE;
            }
        } else if ((s->lsr & UART_LSR_THRE)) {
            return;
        } else {
            s->tsr = s->thr;
            s->lsr |= UART_LSR_THRE;
            s->lsr &= ~UART_LSR_TEMT;
        }
    }

    if (s->mcr & UART_MCR_LOOP) {
        /* in loopback mode, say that we just received a char */
        uart_send(s, s->tsr);
    } else if (sif_send(s->sif, s->tsr) != 1) {
        if ((s->tsr_retry >= 0) && (s->tsr_retry <= MAX_XMIT_RETRY)) {
            s->tsr_retry++;
            timer_set_deadline_ns(&s->transmit_timer,
                                  new_xmit_ts + s->char_transmit_time);
            return;
        } else if (s->poll_msl < 0) {
            /* If we exceed MAX_XMIT_RETRY and the backend is not a real serial
               port, then drop any further failed writes instantly, until we
               get one that goes through. This is to prevent guests that log to
               unconnected pipes or pty's from stalling. */
            s->tsr_retry = -1;
        }
    } else {
        s->tsr_retry = 0;
    }

    s->last_xmit_ts = clock_get_ns(s->clock);
    if (!(s->lsr & UART_LSR_THRE)) {
        timer_set_deadline_ns(&s->transmit_timer,
                              s->last_xmit_ts + s->char_transmit_time);
    }

    if (s->lsr & UART_LSR_THRE) {
        s->lsr |= UART_LSR_TEMT;
        s->thr_ipending = 1;
        _uart_update_irq(s);
    }
}

static void uart_xmit_cb(struct timer *t)
{
    struct uart *s = container_of(t, struct uart, transmit_timer);

    g_mutex_lock(s->lock);
    _uart_xmit(s);
    g_mutex_unlock(s->lock);
}

void uart_io_write(struct uart *s, uint8_t addr, uint8_t val)
{
    g_mutex_lock(s->lock);

    addr &= 7;
    DPRINTF("write addr=0x%02x val=0x%02x\n", addr, val);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            _uart_update_parameters(s);
        } else {
            s->thr = (uint8_t) val;
            if(s->fcr & UART_FCR_FE) {
                _fifo_put(s, XMIT_FIFO, s->thr);
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_TEMT;
                s->lsr &= ~UART_LSR_THRE;
                _uart_update_irq(s);
            } else {
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_THRE;
                _uart_update_irq(s);
            }
            _uart_xmit(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
            _uart_update_parameters(s);
        } else {
            s->ier = val & 0x0f;
            /* If the backend device is a real serial port, turn polling of the
               modem status lines on physical port on or off depending on
               UART_IER_MSI state */
            if (s->poll_msl >= 0) {
                if (s->ier & UART_IER_MSI) {
                    s->poll_msl = 1;
                    _uart_update_msl(s);
                } else {
                    timer_cancel(&s->modem_status_poll);
                    s->poll_msl = 0;
                }
            }
            if (s->lsr & UART_LSR_THRE) {
                s->thr_ipending = 1;
                _uart_update_irq(s);
            }
        }
        break;
    case 2:
        val = val & 0xFF;

        if (s->fcr == val) {
            break;
        }

        /* Did the enable/disable flag change? If so, make sure FIFOs get
           flushed */
        if ((val ^ s->fcr) & UART_FCR_FE) {
            val |= UART_FCR_XFR | UART_FCR_RFR;
        }

        /* FIFO clear */
        if (val & UART_FCR_RFR) {
            timer_cancel(&s->fifo_timeout_timer);
            s->timeout_ipending = 0;
            _fifo_clear(s,RECV_FIFO);
        }

        if (val & UART_FCR_XFR) {
            _fifo_clear(s,XMIT_FIFO);
        }

        if (val & UART_FCR_FE) {
            s->iir |= UART_IIR_FE;
            /* Set RECV_FIFO trigger Level */
            switch (val & 0xC0) {
            case UART_FCR_ITL_1:
                s->recv_fifo.itl = 1;
                break;
            case UART_FCR_ITL_2:
                s->recv_fifo.itl = 4;
                break;
            case UART_FCR_ITL_3:
                s->recv_fifo.itl = 8;
                break;
            case UART_FCR_ITL_4:
                s->recv_fifo.itl = 14;
                break;
            }
        } else {
            s->iir &= ~UART_IIR_FE;
        }

        /* Set fcr - or at least the bits in it that are supposed to "stick" */
        s->fcr = val & 0xC9;
        _uart_update_irq(s);
        break;
    case 3: {
        int break_enable;

        s->lcr = val;
        _uart_update_parameters(s);
        break_enable = (val >> 6) & 1;
        if (break_enable != s->last_break_enable) {
            s->last_break_enable = break_enable;
            sif_set_break(s->sif, break_enable);
        }
        break;
    }
    case 4: {
        int flags;
        int old_mcr = s->mcr;

        s->mcr = val & 0x1f;
        if (val & UART_MCR_LOOP) {
            break;
        }

        if (s->poll_msl >= 0 && old_mcr != s->mcr) {
            flags = sif_get_tiocm(s->sif);
            flags &= ~(SIF_TIOCM_RTS | SIF_TIOCM_DTR);

            if (val & UART_MCR_RTS) {
                flags |= SIF_TIOCM_RTS;
            }
            if (val & UART_MCR_DTR) {
                flags |= SIF_TIOCM_DTR;
            }

            sif_set_tiocm(s->sif, flags);

            /* Update the modem status after a one-character-send wait-time,
               since there may be a response from the device/computer at the
               other end of the serial line */
            timer_set_deadline_rel_ns(&s->modem_status_poll,
                                      s->char_transmit_time);
        }
        break;
    }
    case 5:
        break;
    case 6:
        break;
    case 7:
        s->scr = val;
        break;
    }

    g_mutex_unlock(s->lock);
}

uint8_t uart_io_read(struct uart *s, uint8_t addr)
{
    uint32_t ret;

    g_mutex_lock(s->lock);

    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff;
        } else {
            if (s->fcr & UART_FCR_FE) {
                ret = _fifo_get(s,RECV_FIFO);
                if (s->recv_fifo.count == 0) {
                    s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
                } else {
                    timer_set_deadline_rel_ns(&s->fifo_timeout_timer,
                                              s->char_transmit_time * 4);
                }
                s->timeout_ipending = 0;
            } else {
                ret = s->rbr;
                s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            }
            _uart_update_irq(s);
            if (!(s->mcr & UART_MCR_LOOP)) {
                /* in loopback mode, don't receive any data */
                sif_accept_input(s->sif);
            }
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            ret = (s->divider >> 8) & 0xff;
        } else {
            ret = s->ier;
        }
        break;
    case 2:
        ret = s->iir;
        if ((ret & UART_IIR_ID) == UART_IIR_THRI) {
            s->thr_ipending = 0;
            _uart_update_irq(s);
        }
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        /* Clear break and overrun interrupts */
        if (s->lsr & (UART_LSR_BI|UART_LSR_OE)) {
            s->lsr &= ~(UART_LSR_BI|UART_LSR_OE);
            _uart_update_irq(s);
        }
        break;
    case 6:
        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback, the modem output pins are connected to the
               inputs */
            ret = (s->mcr & 0x0c) << 4;
            ret |= (s->mcr & 0x02) << 3;
            ret |= (s->mcr & 0x01) << 5;
        } else {
            if (s->poll_msl >= 0) {
                _uart_update_msl(s);
            }
            ret = s->msr;
            /* Clear delta bits & msr int after read, if they were set */
            if (s->msr & UART_MSR_ANY_DELTA) {
                s->msr &= 0xF0;
                _uart_update_irq(s);
            }
        }
        break;
    case 7:
        ret = s->scr;
        break;
    }
    DPRINTF("read addr=0x%02x val=0x%02x\n", addr, ret);

    g_mutex_unlock(s->lock);

    return ret;
}

int uart_can_send(struct uart *s)
{
    int value;

    g_mutex_lock(s->lock);

    if (s->fcr & UART_FCR_FE) {
        /* Advertise (fifo.itl - fifo.count) bytes when count < ITL, and 1 if
           above. If UART_FIFO_LENGTH - fifo.count is advertised the effect
           will be to almost always fill the fifo completely before the guest
           has a chance to respond, effectively overriding the ITL that the
           guest has set. */
        if (s->recv_fifo.count < UART_FIFO_LENGTH) {
            if (s->recv_fifo.count <= s->recv_fifo.itl) {
                value = s->recv_fifo.itl - s->recv_fifo.count;
            } else {
                value = 1;
            }
        } else {
            value = 0;
        }
    } else {
        value = !(s->lsr & UART_LSR_DR);
    }

    g_mutex_unlock(s->lock);

    return value;
}

void uart_break(struct uart *s)
{
    g_mutex_lock(s->lock);

    s->rbr = 0;
    /* When the LSR_DR is set a null byte is pushed into the fifo */
    _fifo_put(s, RECV_FIFO, '\0');
    s->lsr |= UART_LSR_BI | UART_LSR_DR;
    _uart_update_irq(s);

    g_mutex_unlock(s->lock);
}

/* There's data in recv_fifo and s->rbr has not been read for 4 char transmit
   times */
static void fifo_timeout_int(struct timer *t)
{
    struct uart *s = container_of(t, struct uart, fifo_timeout_timer);

    g_mutex_lock(s->lock);

    if (s->recv_fifo.count) {
        s->timeout_ipending = 1;
        _uart_update_irq(s);
    }

    g_mutex_unlock(s->lock);
}

void uart_send(struct uart *s, uint8_t value)
{
    g_mutex_lock(s->lock);

    if (s->fcr & UART_FCR_FE) {
        _fifo_put(s, RECV_FIFO, value);
        s->lsr |= UART_LSR_DR;

        /* call the timeout receive callback in 4 char transmit time */
        timer_set_deadline_rel_ns(&s->fifo_timeout_timer,
                                  s->char_transmit_time * 4);
    } else {
        if (s->lsr & UART_LSR_DR) {
            s->lsr |= UART_LSR_OE;
        }
        s->rbr = value;
        s->lsr |= UART_LSR_DR;
    }
    _uart_update_irq(s);

    g_mutex_unlock(s->lock);
}

void uart_reset(struct uart *s)
{
    g_mutex_lock(s->lock);

    s->rbr = 0;
    s->ier = 0;
    s->iir = UART_IIR_NO_INT;
    s->lcr = 0;
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
    s->msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
    /* Default to 9600 baud, 1 start bit, 8 data bits, 1 stop bit, no parity. */
    s->divider = 0x0C;
    s->mcr = UART_MCR_OUT2;
    s->scr = 0;
    s->tsr_retry = 0;
    s->char_transmit_time = (NS_PER_SEC / 9600) * 10;
    s->poll_msl = 0;

    _fifo_clear(s,RECV_FIFO);
    _fifo_clear(s,XMIT_FIFO);

    s->last_xmit_ts = clock_get_ns(s->clock);

    s->thr_ipending = 0;
    s->last_break_enable = 0;

    pin_lower(&s->irq);

    g_mutex_unlock(s->lock);
}

void uart_init(struct uart *s, struct clock *c, struct serial_interface *sif)
{
    s->lock = g_mutex_new();

    s->clock = c;
    s->sif = sif;

    timer_init(&s->modem_status_poll, s->clock, uart_update_msl_cb);
    timer_init(&s->fifo_timeout_timer, s->clock, fifo_timeout_int);
    timer_init(&s->transmit_timer, s->clock, uart_xmit_cb);

    pin_init(&s->irq);
}

void uart_cleanup(struct uart *s)
{
    g_mutex_lock(s->lock);

    timer_cleanup(&s->modem_status_poll);
    timer_cleanup(&s->fifo_timeout_timer);
    timer_cleanup(&s->transmit_timer);

    pin_cleanup(&s->irq);

    g_mutex_unlock(s->lock);

    g_mutex_free(s->lock);
}
