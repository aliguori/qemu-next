#ifndef UART_H
#define UART_H

#include "newemu/timer.h"
#include "newemu/pin.h"
#include "newemu/serial_iface.h"

#include <glib.h>

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */
#define UART_IIR_CTI    0x0C    /* Character Timeout Indication */

#define UART_IIR_FENF   0x80    /* Fifo enabled, but not functionning */
#define UART_IIR_FE     0xC0    /* Fifo enabled */

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_MCR_OUT2	0x08	/* Out2 complement */
#define UART_MCR_OUT1	0x04	/* Out1 complement */
#define UART_MCR_RTS	0x02	/* RTS complement */
#define UART_MCR_DTR	0x01	/* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_RI	0x40	/* Ring Indicator */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_DDCD	0x08	/* Delta DCD */
#define UART_MSR_TERI	0x04	/* Trailing edge ring indicator */
#define UART_MSR_DDSR	0x02	/* Delta DSR */
#define UART_MSR_DCTS	0x01	/* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F	/* Any of the delta bits! */

#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

/* Any of the lsr-interrupt-triggering status bits */
#define UART_LSR_INT_ANY 0x1E

/* Interrupt trigger levels. The byte-counts are for 16550A - in newer UARTs
   the byte-count for each ITL is higher. */

#define UART_FCR_ITL_1      0x00 /* 1 byte ITL */
#define UART_FCR_ITL_2      0x40 /* 4 bytes ITL */
#define UART_FCR_ITL_3      0x80 /* 8 bytes ITL */
#define UART_FCR_ITL_4      0xC0 /* 14 bytes ITL */

#define UART_FCR_DMS        0x08    /* DMA Mode Select */
#define UART_FCR_XFR        0x04    /* XMIT Fifo Reset */
#define UART_FCR_RFR        0x02    /* RCVR Fifo Reset */
#define UART_FCR_FE         0x01    /* FIFO Enable */

#define UART_FIFO_LENGTH    16      /* 16550A Fifo Length */

#define XMIT_FIFO           0
#define RECV_FIFO           1
#define MAX_XMIT_RETRY      4

struct uart_fifo {
    uint8_t data[UART_FIFO_LENGTH];
    uint8_t count;
    uint8_t itl;                        /* Interrupt Trigger Level */
    uint8_t tail;
    uint8_t head;
};

struct uart {
    GMutex *lock;

    uint16_t divider;
    uint8_t rbr; /* receive register */
    uint8_t thr; /* transmit holding register */
    uint8_t tsr; /* transmit shift register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
    uint8_t fcr;
    uint8_t fcr_vmstate; /* we can't write directly this value
                            it has side effects */
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    struct pin irq;
    int last_break_enable;
    int it_shift;
    int baudbase;
    int tsr_retry;
    uint32_t wakeup;

    /* Time when the last byte was successfully sent out of the tsr */
    uint64_t last_xmit_ts;
    struct uart_fifo recv_fifo;
    struct uart_fifo xmit_fifo;

    struct timer fifo_timeout_timer;
    /* timeout interrupt pending state */
    int timeout_ipending;
    struct timer transmit_timer;


    /* time to transmit a char in ticks*/
    uint64_t char_transmit_time;
    int poll_msl;

    struct timer modem_status_poll;

    struct clock *clock;

    struct serial_interface *sif;
};

void uart_init(struct uart *s, struct clock *c, struct serial_interface *sif);

void uart_cleanup(struct uart *s);

void uart_io_write(struct uart *s, uint8_t addr, uint8_t val);

uint8_t uart_io_read(struct uart *s, uint8_t addr);

void uart_break(struct uart *s);

int uart_can_send(struct uart *s);

void uart_send(struct uart *s, uint8_t value);

void uart_reset(struct uart *s);

#endif
