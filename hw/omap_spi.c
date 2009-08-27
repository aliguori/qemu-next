/*
 * TI OMAP processor's Multichannel SPI emulation.
 *
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Original code for OMAP2 by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "omap.h"

//#define SPI_DEBUG

#ifdef SPI_DEBUG
#define TRACE(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", __FUNCTION__, \
                               __LINE__, ##__VA_ARGS__);
#else
#define TRACE(...)
#endif

#define SPI_FIFOSIZE 64
#define SPI_REV_OMAP2420 0x14
#define SPI_REV_OMAP3530 0x21
#define IS_OMAP3_SPI(s) ((s)->revision >= SPI_REV_OMAP3530)

struct omap_mcspi_s {
    qemu_irq irq;
    int chnum;
    uint8_t revision;
    
    uint32_t sysconfig;
    uint32_t systest;
    uint32_t irqst;
    uint32_t irqen;
    uint32_t wken;
    uint32_t control;
    uint32_t xferlevel;
    struct omap_mcspi_fifo_s {
        int start;
        int len;
        int size;
        uint8_t buf[SPI_FIFOSIZE];
    } tx_fifo, rx_fifo;
    int fifo_ch;
    int fifo_wcnt;
    
    struct omap_mcspi_ch_s {
        qemu_irq txdrq;
        qemu_irq rxdrq;
        uint32_t (*txrx)(void *opaque, uint32_t, int);
        void *opaque;
        
        uint32_t tx;
        uint32_t rx;
        
        uint32_t config;
        uint32_t status;
        uint32_t control;
    } ch[0];
};

static inline void omap_mcspi_interrupt_update(struct omap_mcspi_s *s)
{
    qemu_set_irq(s->irq, s->irqst & s->irqen);
}

static inline void omap_mcspi_dmarequest_update(struct omap_mcspi_s *s,
                                                int chnum)
{
    struct omap_mcspi_ch_s *ch = &s->ch[chnum];
    if ((ch->control & 1) &&                         /* EN */
        (ch->config & (1 << 14)) &&                  /* DMAW */
        (ch->status & (1 << 1)) &&                   /* TXS */
        ((ch->config >> 12) & 3) != 1) {             /* TRM */
        if (!IS_OMAP3_SPI(s) ||
            !(ch->config & (1 << 27)) ||             /* FFEW */
            s->tx_fifo.len <= (s->xferlevel & 0x3f)) /* AEL */
            qemu_irq_raise(ch->txdrq);
        else
            qemu_irq_lower(ch->txdrq);
    }
    if ((ch->control & 1) &&                                /* EN */
        (ch->config & (1 << 15)) &&                         /* DMAW */
        (ch->status & (1 << 0)) &&                          /* RXS */
        ((ch->config >> 12) & 3) != 2) {                    /* TRM */
        if (!IS_OMAP3_SPI(s) ||
            !(ch->config & (1 << 28)) ||                    /* FFER */
            s->rx_fifo.len >= ((s->xferlevel >> 8) & 0x3f)) /* AFL */
            qemu_irq_raise(ch->rxdrq);
        else
            qemu_irq_lower(ch->rxdrq);
    }
}

static void omap_mcspi_fifo_reset(struct omap_mcspi_s *s)
{
    struct omap_mcspi_ch_s *ch;
    
    s->tx_fifo.len = 0;
    s->rx_fifo.len = 0;
    s->tx_fifo.start = 0;
    s->rx_fifo.start = 0;
    if (s->fifo_ch < 0) {
        s->tx_fifo.size  = s->rx_fifo.size  = 0;
    } else {
        ch = &s->ch[s->fifo_ch];
        s->tx_fifo.size = ((ch->config >> 27) & 1) ? SPI_FIFOSIZE : 0;
        s->rx_fifo.size = ((ch->config >> 28) & 1) ? SPI_FIFOSIZE : 0;
        if (((ch->config >> 27) & 3) == 3) {
            s->tx_fifo.size >>= 1;
            s->rx_fifo.size >>= 1;
        }
    }
}

static void omap_mcspi_save_state(QEMUFile *f, void *opaque)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *)opaque;
    int i;
    
    qemu_put_be32(f, s->sysconfig);
    qemu_put_be32(f, s->systest);
    qemu_put_be32(f, s->irqst);
    qemu_put_be32(f, s->irqen);
    qemu_put_be32(f, s->wken);
    qemu_put_be32(f, s->control);
    qemu_put_be32(f, s->xferlevel);
    qemu_put_sbe32(f, s->tx_fifo.start);
    qemu_put_sbe32(f, s->tx_fifo.len);
    qemu_put_sbe32(f, s->tx_fifo.size);
    qemu_put_buffer(f, s->tx_fifo.buf, sizeof(s->tx_fifo.buf));
    qemu_put_sbe32(f, s->rx_fifo.start);
    qemu_put_sbe32(f, s->rx_fifo.len);
    qemu_put_sbe32(f, s->rx_fifo.size);
    qemu_put_buffer(f, s->rx_fifo.buf, sizeof(s->rx_fifo.buf));
    qemu_put_sbe32(f, s->fifo_ch);
    qemu_put_sbe32(f, s->fifo_wcnt);
    for (i = 0; i < s->chnum; i++) {
        qemu_put_be32(f, s->ch[i].tx);
        qemu_put_be32(f, s->ch[i].rx);
        qemu_put_be32(f, s->ch[i].config);
        qemu_put_be32(f, s->ch[i].status);
        qemu_put_be32(f, s->ch[i].control);
    }
}

static int omap_mcspi_load_state(QEMUFile *f, void *opaque, int version_id)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *)opaque;
    int i;
    
    if (version_id)
        return -EINVAL;
    
    s->sysconfig = qemu_get_be32(f);
    s->systest = qemu_get_be32(f);
    s->irqst = qemu_get_be32(f);
    s->irqen = qemu_get_be32(f);
    s->wken = qemu_get_be32(f);
    s->control = qemu_get_be32(f);
    s->xferlevel = qemu_get_be32(f);
    s->tx_fifo.start = qemu_get_be32(f);
    s->tx_fifo.len = qemu_get_be32(f);
    s->tx_fifo.size = qemu_get_be32(f);
    qemu_get_buffer(f, s->tx_fifo.buf, sizeof(s->tx_fifo.buf));
    s->rx_fifo.start = qemu_get_be32(f);
    s->rx_fifo.len = qemu_get_be32(f);
    s->rx_fifo.size = qemu_get_be32(f);
    qemu_get_buffer(f, s->rx_fifo.buf, sizeof(s->rx_fifo.buf));
    s->fifo_ch = qemu_get_sbe32(f);
    s->fifo_wcnt = qemu_get_sbe32(f);
    for (i = 0; i < s->chnum; i++) {
        s->ch[i].tx = qemu_get_be32(f);
        s->ch[i].rx = qemu_get_be32(f);
        s->ch[i].config = qemu_get_be32(f);
        s->ch[i].status = qemu_get_be32(f);
        s->ch[i].control = qemu_get_be32(f);
        omap_mcspi_dmarequest_update(s, i);
    }
    omap_mcspi_interrupt_update(s);
    
    return 0;
}

/* returns next word in FIFO or the n first bytes if there is not
 * enough data in FIFO */
static uint32_t omap_mcspi_fifo_get(struct omap_mcspi_fifo_s *s, int wl)
{
    uint32_t v, sh;
    
    for (v = 0, sh = 0; wl > 0 && s->len; wl -= 8, s->len--, sh += 8) {
        v |= ((uint32_t)s->buf[s->start++]) << sh;
        if (s->start >= s->size)
            s->start = 0;
    }
    return v;
}

/* pushes a word to FIFO or the first n bytes of the word if the FIFO
 * is too full to hold the full word */
static void omap_mcspi_fifo_put(struct omap_mcspi_fifo_s *s, int wl,
                                uint32_t v)
{
    int p = s->start + s->len;

    for (; wl > 0 && s->len < s->size; wl -=8, v >>= 8, s->len++) {
        if (p >= s->size)
            p -= s->size;
        s->buf[p++] = (uint8_t)(v & 0xff);
    }
}

static void omap_mcspi_transfer_run(struct omap_mcspi_s *s, int chnum)
{
    struct omap_mcspi_ch_s *ch = s->ch + chnum;
    int trm = (ch->config >> 12) & 3;
    int wl;
    
    if (!(ch->control & 1))                  /* EN */
        return;
    if ((ch->status & 1) && trm != 2 &&      /* RXS */
        !(ch->config & (1 << 19)))           /* TURBO */
        goto intr_update;
    if ((ch->status & (1 << 1)) && trm != 1) /* TXS */
        goto intr_update;
    
    if (!(s->control & 1) ||        /* SINGLE */
        (ch->config & (1 << 20))) { /* FORCE */
        if (ch->txrx) {
            wl = 1 + (0x1f & (ch->config >> 7)); /* WL */
            if (!IS_OMAP3_SPI(s) || s->fifo_ch != chnum ||
                !((ch->config >> 27) & 3))       /* FFER | FFEW */
                ch->rx = ch->txrx(ch->opaque, ch->tx, wl);
            else {
                switch ((ch->config >> 27) & 3) {
                    case 1: /* !FFER, FFEW */
                        if (trm != 1)
                            ch->tx = omap_mcspi_fifo_get(&s->tx_fifo, wl);
                        ch->rx = ch->txrx(ch->opaque, ch->tx, wl);
                        s->fifo_wcnt--;
                        break;
                    case 2: /* FFER, !FFEW */
                        ch->rx = ch->txrx(ch->opaque, ch->tx, wl);
                        if (trm != 2)
                            omap_mcspi_fifo_put(&s->rx_fifo, wl, ch->rx);
                        s->fifo_wcnt--;
                        break;
                    case 3: /* FFER, FFEW */
                        while (s->rx_fifo.len < s->rx_fifo.size &&
                               s->tx_fifo.len && s->fifo_wcnt) {
                            if (trm != 1)
                                ch->tx = omap_mcspi_fifo_get(&s->tx_fifo, wl);
                            ch->rx = ch->txrx(ch->opaque, ch->tx, wl);
                            if (trm != 2)
                                omap_mcspi_fifo_put(&s->rx_fifo, wl, ch->rx);
                            s->fifo_wcnt--;
                        }
                        break;
                    default:
                        break;
                }
                if ((ch->config & (1 << 28)) &&        /* FFER */
                    s->rx_fifo.len >= s->rx_fifo.size)
                    ch->status |= 1 << 6;              /* RXFFF */
                ch->status &= ~(1 << 5);               /* RXFFE */
                ch->status &= ~(1 << 4);               /* TXFFF */
                if ((ch->config & (1 << 27)) &&        /* FFEW */
                    !s->tx_fifo.len)
                    ch->status |= 1 << 3;              /* TXFFE */
                if (!s->fifo_wcnt &&
                    ((s->xferlevel >> 16) & 0xffff))   /* WCNT */
                    s->irqst |= 1 << 17;               /* EOW */
            }
        }
    }
    
    ch->tx = 0;
    ch->status |= 1 << 2;               /* EOT */
    ch->status |= 1 << 1;               /* TXS */
    if (trm != 2)
        ch->status |= 1;                /* RXS */
        
intr_update:
    if ((ch->status & 1) &&	trm != 2 &&                     /* RXS */
        !(ch->config & (1 << 19)))                          /* TURBO */
        if (!IS_OMAP3_SPI(s) || s->fifo_ch != chnum ||
            !((ch->config >> 28) & 1) ||                    /* FFER */
            s->rx_fifo.len >= ((s->xferlevel >> 8) & 0x3f)) /* AFL */
            s->irqst |= 1 << (2 + 4 * chnum);               /* RX_FULL */
    if ((ch->status & (1 << 1)) && trm != 1)                /* TXS */
        if (!IS_OMAP3_SPI(s) || s->fifo_ch != chnum ||
            !((ch->config >> 27) & 1) ||                    /* FFEW */
            s->tx_fifo.len <= (s->xferlevel & 0x3f))        /* AEL */
            s->irqst |= 1 << (4 * chnum);                   /* TX_EMPTY */
    omap_mcspi_interrupt_update(s);
    omap_mcspi_dmarequest_update(s, chnum);
}

void omap_mcspi_reset(struct omap_mcspi_s *s)
{
    int ch;
    
    s->sysconfig = 0;
    s->systest = 0;
    s->irqst = 0;
    s->irqen = 0;
    s->wken = 0;
    s->control = 4;
    
    s->fifo_ch = -1;
    omap_mcspi_fifo_reset(s);
    
    for (ch = 0; ch < s->chnum; ch ++) {
        s->ch[ch].config = 0x060000;
        s->ch[ch].status = 2;				/* TXS */
        s->ch[ch].control = 0;
        
        omap_mcspi_dmarequest_update(s, ch);
    }
    
    omap_mcspi_interrupt_update(s);
}

static uint32_t omap_mcspi_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    int ch = 0;
    uint32_t ret;
    
    switch (addr) {
        case 0x00:	/* MCSPI_REVISION */
            return s->revision;
            
        case 0x10:	/* MCSPI_SYSCONFIG */
            return s->sysconfig;
            
        case 0x14:	/* MCSPI_SYSSTATUS */
            return 1;					/* RESETDONE */
            
        case 0x18:	/* MCSPI_IRQSTATUS */
            return s->irqst;
            
        case 0x1c:	/* MCSPI_IRQENABLE */
            return s->irqen;
            
        case 0x20:	/* MCSPI_WAKEUPENABLE */
            return s->wken;
            
        case 0x24:	/* MCSPI_SYST */
            return s->systest;
            
        case 0x28:	/* MCSPI_MODULCTRL */
            return s->control;
            
        case 0x68: ch ++;
        case 0x54: ch ++;
        case 0x40: ch ++;
        case 0x2c:	/* MCSPI_CHCONF */
            return (ch < s->chnum) ? s->ch[ch].config : 0;
            
        case 0x6c: ch ++;
        case 0x58: ch ++;
        case 0x44: ch ++;
        case 0x30:	/* MCSPI_CHSTAT */
            return (ch < s->chnum) ? s->ch[ch].status : 0;
            
        case 0x70: ch ++;
        case 0x5c: ch ++;
        case 0x48: ch ++;
        case 0x34:	/* MCSPI_CHCTRL */
            return (ch < s->chnum) ? s->ch[ch].control : 0;
            
        case 0x74: ch ++;
        case 0x60: ch ++;
        case 0x4c: ch ++;
        case 0x38:	/* MCSPI_TX */
            if (ch < s->chnum)
                return s->ch[ch].tx;
            break;
            
        case 0x78: ch ++;
        case 0x64: ch ++;
        case 0x50: ch ++;
        case 0x3c:	/* MCSPI_RX */
            if (ch < s->chnum) {
                if (!IS_OMAP3_SPI(s) || ch != s->fifo_ch ||
                    !(s->ch[ch].config & (1 << 28))) { /* FFER */
                    s->ch[ch].status &= ~1;            /* RXS */
                    ret = s->ch[ch].rx;
                    omap_mcspi_transfer_run(s, ch);
                    return ret;
                }
                if (!s->rx_fifo.len) {
                    TRACE("rxfifo underflow!");
                } else {
                    qemu_irq_lower(s->ch[ch].rxdrq);
                    s->ch[ch].status &= ~(1 << 6);                 /* RXFFF */
                    if (((s->ch[ch].config >> 12) & 3) != 2)        /* TRM */
                        ret = omap_mcspi_fifo_get(&s->rx_fifo,
                            1 + ((s->ch[ch].config >> 7) & 0x1f)); /* WL */
                    else
                        ret = s->ch[ch].rx;
                    if (!s->rx_fifo.len) {
                        s->ch[ch].status &= ~1;     /* RXS */
                        s->ch[ch].status |= 1 << 5; /* RXFFE */
                        omap_mcspi_transfer_run(s, ch);
                    }
                    return ret;
                }
            }
            return 0;
        
        case 0x7c: /* MCSPI_XFERLEVEL */
            if (IS_OMAP3_SPI(s)) {
                if ((s->xferlevel >> 16) & 0xffff) /* WCNT */
                    ret = ((s->xferlevel & 0xffff0000) - (s->fifo_wcnt << 16));
                else
                    ret = ((-s->fifo_wcnt) & 0xffff) << 16;
                return (s->xferlevel & 0xffff) | ret;
            }
            break;
            
        default:
            break;
    }
    
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mcspi_write(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    uint32_t old;
    int ch = 0;
    
    switch (addr) {
        case 0x00:	/* MCSPI_REVISION */
        case 0x14:	/* MCSPI_SYSSTATUS */
        case 0x30:	/* MCSPI_CHSTAT0 */
        case 0x3c:	/* MCSPI_RX0 */
        case 0x44:	/* MCSPI_CHSTAT1 */
        case 0x50:	/* MCSPI_RX1 */
        case 0x58:	/* MCSPI_CHSTAT2 */
        case 0x64:	/* MCSPI_RX2 */
        case 0x6c:	/* MCSPI_CHSTAT3 */
        case 0x78:	/* MCSPI_RX3 */
            /* silently ignore */
            //OMAP_RO_REGV(addr, value);
            return;
            
        case 0x10:	/* MCSPI_SYSCONFIG */
            if (value & (1 << 1))				/* SOFTRESET */
                omap_mcspi_reset(s);
            s->sysconfig = value & 0x31d;
            break;
            
        case 0x18:	/* MCSPI_IRQSTATUS */
            if (!((s->control & (1 << 3)) && (s->systest & (1 << 11)))) {
                s->irqst &= ~value;
                omap_mcspi_interrupt_update(s);
            }
            break;
            
        case 0x1c:	/* MCSPI_IRQENABLE */
            s->irqen = value & (IS_OMAP3_SPI(s) ? 0x3777f : 0x1777f);
            omap_mcspi_interrupt_update(s);
            break;
            
        case 0x20:	/* MCSPI_WAKEUPENABLE */
            s->wken = value & 1;
            break;
            
        case 0x24:	/* MCSPI_SYST */
            if (s->control & (1 << 3))			/* SYSTEM_TEST */
                if (value & (1 << 11)) {			/* SSB */
                    s->irqst |= 0x1777f;
                    omap_mcspi_interrupt_update(s);
                }
            s->systest = value & 0xfff;
            break;
            
        case 0x28:	/* MCSPI_MODULCTRL */
            if (value & (1 << 3))				/* SYSTEM_TEST */
                if (s->systest & (1 << 11)) {		/* SSB */
                    s->irqst |= IS_OMAP3_SPI(s) ? 0x3777f : 0x1777f;
                    omap_mcspi_interrupt_update(s);
                }
            s->control = value & 0xf;
            break;
            
        case 0x68: ch ++;
        case 0x54: ch ++;
        case 0x40: ch ++;
        case 0x2c:	/* MCSPI_CHCONF */
            if (ch < s->chnum) {
                old = s->ch[ch].config;
                s->ch[ch].config = value & (IS_OMAP3_SPI(s)
                                            ? 0x3fffffff : 0x7fffff);
                if (IS_OMAP3_SPI(s) &&
                    ((value ^ old) & (3 << 27))) { /* FFER | FFEW */
                    s->fifo_ch = ((value & (3 << 27))) ? ch : -1;
                    omap_mcspi_fifo_reset(s);
                }
                if (((value ^ old) & (3 << 14)) || /* DMAR | DMAW */
                    (IS_OMAP3_SPI(s) &&
                     ((value ^ old) & (3 << 27)))) /* FFER | FFEW */
                    omap_mcspi_dmarequest_update(s, ch);
                if (((value >> 12) & 3) == 3) {   /* TRM */
                    TRACE("invalid TRM value (3)");
                }
                if (((value >> 7) & 0x1f) < 3) {  /* WL */
                    TRACE("invalid WL value (%i)", (value >> 7) & 0x1f);
                }
                if (IS_OMAP3_SPI(s) && ((value >> 23) & 1)) { /* SBE */
                    TRACE("start-bit mode is not supported");
                }
            }
            break;
            
        case 0x70: ch ++;
        case 0x5c: ch ++;
        case 0x48: ch ++;
        case 0x34:	/* MCSPI_CHCTRL */
            if (ch < s->chnum) {
                old = s->ch[ch].control;
                s->ch[ch].control = value & (IS_OMAP3_SPI(s) ? 0xff01 : 1);
                if (value & ~old & 1) { /* EN */
                    if (IS_OMAP3_SPI(s) && s->fifo_ch == ch)
                        omap_mcspi_fifo_reset(s);
                    omap_mcspi_transfer_run(s, ch);
                }
            }
            break;
            
        case 0x74: ch ++;
        case 0x60: ch ++;
        case 0x4c: ch ++;
        case 0x38:	/* MCSPI_TX */
            if (ch < s->chnum) {
                if (!IS_OMAP3_SPI(s) || s->fifo_ch != ch ||
                    !(s->ch[ch].config & (1 << 27))) { /* FFEW */
                    s->ch[ch].tx = value;
                    s->ch[ch].status &= ~(1 << 1);     /* TXS */
                    omap_mcspi_transfer_run(s, ch);
                } else {
                    if (s->tx_fifo.len >= s->tx_fifo.size) {
                        TRACE("txfifo overflow!");
                    } else {
                        qemu_irq_lower(s->ch[ch].txdrq);
                        s->ch[ch].status &= ~0x0a;            /* TXFFE | TXS */
                        if (((s->ch[ch].config >> 12) & 3) != 1) {    /* TRM */
                            omap_mcspi_fifo_put(
                                &s->tx_fifo,
                                1 + ((s->ch[ch].config >> 7) & 0x1f), /* WL */
                                value);
                            if (s->tx_fifo.len >= s->tx_fifo.size)
                                s->ch[ch].status |= 1 << 4;        /* TXFFF */
                            if (s->tx_fifo.len >= (s->xferlevel & 0x3f))
                                omap_mcspi_transfer_run(s, ch);
                        } else {
                            s->ch[ch].tx = value;
                            omap_mcspi_transfer_run(s, ch);
                        }
                    }
                }
            }
            break;
            
        case 0x7c:
            if (IS_OMAP3_SPI(s)) {
                if (value != s->xferlevel) {
                    s->fifo_wcnt = (value >> 16) & 0xffff;
                    s->xferlevel = value & 0xffff3f3f;
                    omap_mcspi_fifo_reset(s);
                }
            } else
                OMAP_BAD_REGV(addr, value);
            break;
            
        default:
            OMAP_BAD_REGV(addr, value);
            return;
    }
}

static CPUReadMemoryFunc *omap_mcspi_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_mcspi_read,
};

static CPUWriteMemoryFunc *omap_mcspi_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_mcspi_write,
};

struct omap_mcspi_s *omap_mcspi_init(struct omap_target_agent_s *ta,
                                     struct omap_mpu_state_s *mpu,
                                     int chnum, qemu_irq irq, qemu_irq *drq,
                                     omap_clk fclk, omap_clk iclk)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *)
        qemu_mallocz(sizeof(struct omap_mcspi_s) +
                     chnum * sizeof(struct omap_mcspi_ch_s));
    struct omap_mcspi_ch_s *ch = s->ch;
    
    s->irq = irq;
    s->chnum = chnum;
    /* revision was hardcoded as 0x91 in original code -- odd */
    s->revision = cpu_class_omap3(mpu) ? SPI_REV_OMAP3530 : SPI_REV_OMAP2420;
    while (chnum --) {
        ch->txdrq = *drq ++;
        ch->rxdrq = *drq ++;
        ch ++;
    }
    omap_mcspi_reset(s);
    
    omap_l4_attach(ta, 0, l4_register_io_memory(omap_mcspi_readfn,
                                                omap_mcspi_writefn, s));
    register_savevm("omap_mcspi", (ta->base >> 8), 0,
                    omap_mcspi_save_state, omap_mcspi_load_state, s);
    return s;
}

void omap_mcspi_attach(struct omap_mcspi_s *s,
                       uint32_t (*txrx)(void *opaque, uint32_t, int),
                       void *opaque,
                       int chipselect)
{
    if (chipselect < 0 || chipselect >= s->chnum)
        hw_error("%s: Bad chipselect %i\n", __FUNCTION__, chipselect);
    
    s->ch[chipselect].txrx = txrx;
    s->ch[chipselect].opaque = opaque;
}
