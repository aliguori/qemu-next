/*
 * QEMU model of the Xilinx SPI Controller
 *
 * Copyright (C) 2010 Edgar E. Iglesias.
 * Copyright (C) 2012 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>
 * Copyright (C) 2012 PetaLogix
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

#include "sysbus.h"
#include "sysemu.h"
#include "ptimer.h"
#include "qemu-log.h"

#include "ssi.h"

#ifdef XILINX_SPI_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

#define R_DGIER     (0x1c / 4)
#define R_DGIER_IE  (1 << 31)

#define R_IPISR     (0x20 / 4)
#define IRQ_DRR_NOT_EMPTY    (1 << (31 - 23))
#define IRQ_DRR_OVERRUN      (1 << (31 - 26))
#define IRQ_DRR_FULL         (1 << (31 - 27))
#define IRQ_TX_FF_HALF_EMPTY (1 << 6)
#define IRQ_DTR_UNDERRUN     (1 << 3)
#define IRQ_DTR_EMPTY        (1 << (31 - 29))

#define R_IPIER     (0x28 / 4)
#define R_SRR       (0x40 / 4)
#define R_SPICR     (0x60 / 4)
#define R_SPICR_TXFF_RST     (1 << 5)
#define R_SPICR_RXFF_RST     (1 << 6)
#define R_SPICR_MTI          (1 << 8)

#define R_SPISR     (0x64 / 4)
#define SR_TX_FULL    (1 << 3)
#define SR_TX_EMPTY   (1 << 2)
#define SR_RX_FULL    (1 << 1)
#define SR_RX_EMPTY   (1 << 0)


#define R_SPIDTR    (0x68 / 4)
#define R_SPIDRR    (0x6C / 4)
#define R_SPISSR    (0x70 / 4)
#define R_TX_FF_OCY (0x74 / 4)
#define R_RX_FF_OCY (0x78 / 4)
#define R_MAX       (0x7C / 4)

struct XilinxSPI {
    SysBusDevice busdev;
    MemoryRegion mmio;
    qemu_irq irq;
    int irqline;

    QEMUBH *bh;
    ptimer_state *ptimer;

    SSIBus *spi;

    uint32_t c_fifo_exist;

    uint8_t rx_fifo[256];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    uint8_t tx_fifo[256];
    unsigned int tx_fifo_pos;
    unsigned int tx_fifo_len;

    /* Slave select.  */
    uint8_t num_cs;
    int cmd_ongoing;

    uint32_t regs[R_MAX];
};

static void txfifo_reset(struct XilinxSPI *s)
{
    s->tx_fifo_pos = 0;
    s->tx_fifo_len = 0;

    s->regs[R_SPISR] &= ~SR_TX_FULL;
    s->regs[R_SPISR] |= SR_TX_EMPTY;
    s->regs[R_SPISR] &= ~SR_TX_FULL;
    s->regs[R_IPISR] |= IRQ_DTR_EMPTY;
}

static void rxfifo_reset(struct XilinxSPI *s)
{
    s->rx_fifo_pos = 0;
    s->rx_fifo_len = 0;

    s->regs[R_SPISR] |= SR_RX_EMPTY;
    s->regs[R_SPISR] &= ~SR_RX_FULL;
    s->regs[R_IPISR] &= ~IRQ_DRR_NOT_EMPTY;
    s->regs[R_IPISR] &= ~IRQ_DRR_OVERRUN;
}

static void xlx_spi_reset(struct XilinxSPI *s)
{
    memset(s->regs, 0, sizeof s->regs);

    rxfifo_reset(s);
    txfifo_reset(s);

    s->regs[R_SPISSR] = 1;
    ssi_select_slave(s->spi, 0);
}

static void xlx_spi_update_irq(struct XilinxSPI *s)
{
    uint32_t pending;
    pending = s->regs[R_IPISR] & s->regs[R_IPIER];

    pending = pending && (s->regs[R_DGIER] & R_DGIER_IE);
    pending = !!pending;

    /* This call lies right in the data paths so dont call the
       irq chain unless things really changed.  */
    if (pending != s->irqline) {
        s->irqline = pending;
        DB_PRINT("irq_change_of of state %d\n", pending);
        qemu_set_irq(s->irq, pending);
    }
}

static inline int spi_master_enabled(struct XilinxSPI *s)
{
    return !(s->regs[R_SPICR] & R_SPICR_MTI);
}

static int spi_slave_select(struct XilinxSPI *s, uint32_t v)
{
    unsigned int ss;

    ss = ffs(v) - 1;
    return ss < s->num_cs ? ss : SSI_SLAVE_SELECT_NONE;
}

static inline int txfifo_empty(struct XilinxSPI *s)
{
    return s->tx_fifo_len == 0;
}

static inline int txfifo_full(struct XilinxSPI *s)
{
    return s->tx_fifo_len >= ARRAY_SIZE(s->tx_fifo);
}

static inline int rxfifo_empty(struct XilinxSPI *s)
{
    return s->rx_fifo_len == 0;
}

static inline int rxfifo_full(struct XilinxSPI *s)
{
    return s->rx_fifo_len >= ARRAY_SIZE(s->rx_fifo);
}

static inline void txfifo_put(struct XilinxSPI *s, uint8_t v)
{
    s->regs[R_SPISR] &= ~SR_TX_EMPTY;
    s->regs[R_IPISR] &= ~IRQ_DTR_EMPTY;

    s->tx_fifo[s->tx_fifo_pos] = v;
    s->tx_fifo_pos++;
    s->tx_fifo_pos &= ARRAY_SIZE(s->tx_fifo) - 1;
    s->tx_fifo_len++;

    s->regs[R_SPISR] &= ~SR_TX_FULL;
    if (txfifo_full(s)) {
        s->regs[R_SPISR] |= SR_TX_FULL;
    }
}

static inline uint8_t txfifo_get(struct XilinxSPI *s)
{
    uint8_t r = 0;
    assert(s->tx_fifo_len);

    r = s->tx_fifo[(s->tx_fifo_pos - s->tx_fifo_len) &
                                (ARRAY_SIZE(s->tx_fifo) - 1)];
    s->tx_fifo_len--;

    s->regs[R_SPISR] &= ~SR_TX_FULL;
    if (txfifo_empty(s)) {
        s->regs[R_SPISR] |= SR_TX_EMPTY;
        s->regs[R_IPISR] |= IRQ_DTR_EMPTY;
    }

    return r;
}

static inline void rxfifo_put(struct XilinxSPI *s, uint8_t v)
{
    DB_PRINT("%x\n", v);
    s->regs[R_SPISR] &= ~SR_RX_EMPTY;
    s->regs[R_IPISR] |= IRQ_DRR_NOT_EMPTY;

    s->rx_fifo[s->rx_fifo_pos] = v;
    s->rx_fifo_pos++;
    s->rx_fifo_pos &= ARRAY_SIZE(s->rx_fifo) - 1;
    s->rx_fifo_len++;

    s->regs[R_SPISR] &= ~SR_RX_FULL;
    if (s->rx_fifo_len >= ARRAY_SIZE(s->rx_fifo)) {
        s->regs[R_SPISR] |= SR_RX_FULL;
        s->regs[R_IPISR] |= IRQ_DRR_OVERRUN;
    }
}

static inline uint32_t rxfifo_get(struct XilinxSPI *s)
{
    uint32_t r = 0;
    assert(s->rx_fifo_len);

    r = s->rx_fifo[(s->rx_fifo_pos - s->rx_fifo_len) &
                            (ARRAY_SIZE(s->rx_fifo) - 1)];
    s->rx_fifo_len--;

    s->regs[R_SPISR] &= ~SR_RX_FULL;
    if (rxfifo_empty(s)) {
        s->regs[R_SPISR] |= SR_RX_EMPTY;
        s->regs[R_IPISR] &= ~IRQ_DRR_NOT_EMPTY;
    }

    return r;
}

static void spi_timer_run(struct XilinxSPI *s, int delay)
{
    ptimer_set_count(s->ptimer, delay);
    ptimer_run(s->ptimer, 1);
}

static void
spi_flush_txfifo(struct XilinxSPI *s)
{
    uint32_t tx;
    uint32_t rx;

    while (!txfifo_empty(s)) {
        tx = (uint32_t)txfifo_get(s);
        DB_PRINT("data transfer:%x\n", tx);
        rx = ssi_transfer(s->spi, (uint32_t)tx);
        rxfifo_put(s, rx);
    }
}

static uint64_t
spi_read(void *opaque, target_phys_addr_t addr, unsigned int size)
{
    struct XilinxSPI *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SPIDRR:
        if (rxfifo_empty(s)) {
            DB_PRINT("Read from empty FIFO!\n");
            return 0xdeadbeef;
        }

        r = rxfifo_get(s);
        break;

    case R_SPISR:
        r = s->regs[addr];
        if (rxfifo_empty(s)) {
            spi_timer_run(s, 1);
        }
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            r = s->regs[addr];
        }
        break;

    }
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, r);
    xlx_spi_update_irq(s);
    return r;
}

static void
spi_write(void *opaque, target_phys_addr_t addr,
            uint64_t val64, unsigned int size)
{
    struct XilinxSPI *s = opaque;
    uint32_t value = val64;

    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr, value);
    addr >>= 2;
    switch (addr) {
    case R_SRR:
        if (value != 0xa) {
            DB_PRINT("Invalid write to SRR %x\n", value);
        } else {
            xlx_spi_reset(s);
        }
        break;

    case R_SPIDTR:
        txfifo_put(s, value);

        if (!spi_master_enabled(s)) {
            goto done;
        } else {
            DB_PRINT("DTR and master enabled?\n");
        }
        spi_flush_txfifo(s);
        break;

    case R_SPISR:
        DB_PRINT("Invalid write to SPISR %x\n", value);
        break;

    case R_IPISR:
        /* Toggle the bits.  */
        s->regs[addr] ^= value;
        break;

    /* Slave Select Register.  */
    case R_SPISSR:
        ssi_select_slave(s->spi, spi_slave_select(s, ~value));
        s->regs[addr] = value;
        break;

    case R_SPICR:
        /* FIXME: reset irq and sr state to empty queues.  */
        if (value & R_SPICR_RXFF_RST) {
            rxfifo_reset(s);
        }

        if (value & R_SPICR_TXFF_RST) {
            txfifo_reset(s);
        }
        value &= ~(R_SPICR_RXFF_RST | R_SPICR_TXFF_RST);
        s->regs[addr] = value;

        if (!(value & R_SPICR_MTI)) {
            /* When releasing the master disable, initiate a timer
               that eventually will flush the txfifo.  */
            spi_timer_run(s, 1);
        }
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            s->regs[addr] = value;
        }
        break;
    }

done:
    xlx_spi_update_irq(s);
}

static const MemoryRegionOps spi_ops = {
    .read = spi_read,
    .write = spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void timer_hit(void *opaque)
{
    struct XilinxSPI *s = opaque;

    if (!txfifo_empty(s)) {
        spi_flush_txfifo(s);
        s->cmd_ongoing = 1;
    }
    xlx_spi_update_irq(s);
}

static int xilinx_spi_init(SysBusDevice *dev)
{
    struct XilinxSPI *s = FROM_SYSBUS(typeof(*s), dev);

    DB_PRINT("\n");
    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->mmio, &spi_ops, s, "xilinx-spi", R_MAX * 4);
    sysbus_init_mmio(dev, &s->mmio);

    s->bh = qemu_bh_new(timer_hit, s);
    s->ptimer = ptimer_init(s->bh);
    ptimer_set_freq(s->ptimer, 10 * 1000 * 1000);

    s->spi = ssi_create_bus(&dev->qdev, "spi");

    xlx_spi_reset(s);
    return 0;
}

static Property xilinx_spi_properties[] = {
    DEFINE_PROP_UINT8("num-cs", struct XilinxSPI, num_cs, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = xilinx_spi_init;
    dc->props = xilinx_spi_properties;
}

static TypeInfo xilinx_spi_info = {
    .name           = "xilinx,spi",
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(struct XilinxSPI),
    .class_init     = xilinx_spi_class_init,
};

static void xilinx_spi_register_types(void)
{
    type_register_static(&xilinx_spi_info);
}

type_init(xilinx_spi_register_types)
