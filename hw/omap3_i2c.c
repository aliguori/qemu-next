/*
* TI OMAP3 on-chip I2C controller.  
*
* Copyright (C) 2008 yajin <yajin@vm-kernel.org>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of
* the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston,
* MA 02111-1307 USA
*/

#include "hw.h"
#include "i2c.h"
#include "omap.h"

#define OMAP3_TX_FIFO_LEN     0x8
#define OMAP3_RX_FIFO_LEN     0x8

//#define DEBUG

#ifdef DEBUG
FILE *fp;
#endif

struct omap3_i2c_s
{
    qemu_irq irq;
    qemu_irq drq[2];
    //i2c_slave slave;   /*slave mode is not supported yet*/
    i2c_bus *bus;
    
    uint16_t rev;
    uint16_t ie;
    uint16_t stat;
    uint16_t we;
    uint16_t syss;
    uint16_t buf;
    uint16_t cnt;
    uint16_t data;
    uint16_t sysc;
    uint16_t con;
    uint16_t oa0;
    uint16_t sa;
    uint16_t psc;
    uint16_t scll;
    uint16_t sclh;
    uint16_t systest;
    uint16_t bufstat;
    uint16_t oa1;
    uint16_t oa2;
    uint16_t oa3;
    uint16_t actoa;
    uint16_t sblock;
    
    uint8 tx_fifo[OMAP3_TX_FIFO_LEN];
    uint8 rx_fifo[OMAP3_RX_FIFO_LEN];
    int tx_pos;
    int rx_pos;
    
    uint16_t xtrsh;             /*tx buffer threshold */
    uint16_t rtrsh;             /*rx buffer threshold */
    uint8_t xdma_en;            /*tx dma en */
    uint8_t rdma_en;            /*rx dma en */
    
    uint8_t count_cur;
    //uint8_t start_condition;
};

#ifdef DEBUG
static void debug_init()
{
    fp = fopen("omap3_i2c_debug.txt", "w+");
    if (fp == NULL)
    {
        fprintf(stderr, "can not open nandflash_debug.txt \n");
        exit(-1);
    }
    
}
static void debug_out(const char *format, ...)
{
    va_list ap;
    //if (cpu_single_env->regs[15] < 0xc0000000)
    //   return;
    if (fp)
    {
        va_start(ap, format);
        vfprintf(fp, format, ap);
        fflush(fp);
        va_end(ap);
    }
}
#else
#define debug_init()
#define debug_out(...)
#endif

static void omap3_i2c_interrupts_update(struct omap3_i2c_s *s)
{
    debug_out("omap3_i2c_interrupts_update s->stat  %x s->ie %x \n", s->stat,
              s->ie);
    qemu_set_irq(s->irq, s->stat & s->ie);
    /*todo:DMA Interrupt */
#if 0
    if (s->rdma_en)             /* RDMA_EN */
        qemu_set_irq(s->drq[0], (s->stat >> 3) & 1);    /* RRDY */
    if (s->xdma_en)             /* XDMA_EN */
        qemu_set_irq(s->drq[1], (s->stat >> 4) & 1);    /* XRDY */
#endif
}

static void omap3_i2c_regenerate_stat2(struct omap3_i2c_s *s)
{
    
    debug_out
    ("omap3_i2c_regenerate_stat set s->con %x s->cnt %x s->tx_pos %x s->xtrsh %x  \n",
     s->con, s->cnt, s->tx_pos, s->xtrsh);
    if ((s->con >> 9) & 1)
    {
        if (s->tx_pos <= s->xtrsh)
        {
            s->stat |= 1 << 4;  /* XRDY */
            s->stat &= ~(1 << 14);
        }
        else if ((s->cnt - s->tx_pos) < s->xtrsh)
        {
            s->stat &= ~(1 << 4);       /* XRDY */
            s->stat |= 1 << 14;
            /*XDR*/ s->bufstat &= 0xffc0;
            s->bufstat |= s->cnt - s->tx_pos;
        }
    }
    else
    {
        if (s->rx_pos > s->rtrsh)
        {
            s->stat |= 1 << 3;  /* RRDY */
        }
        else
            s->stat &= ~(1 << 3);       /* RRDY */
    }
    
    
}

static void omap3_i2c_stt_set(struct omap3_i2c_s *s, uint32_t value)
{
    debug_out("omap3_i2c_stt_set \n");
    i2c_start_transfer(s->bus, s->sa,   /* SA */
                       (~value >> 9) & 1);      /* TRX */
    s->con &= ~(1 << 0);        /* STT */
}

static void omap3_i2c_stp_set(struct omap3_i2c_s *s)
{
    debug_out("omap3_i2c_stp_set \n");
    if (!i2c_bus_busy(s->bus))
        return;
    i2c_end_transfer(s->bus);
    s->con &= ~(1 << 1);        /* STP */
    s->tx_pos = 0;
}

static void omap3_i2c_fifo_run1(struct omap3_i2c_s *s)
{
    int ack = 1;
    
    if (!i2c_bus_busy(s->bus))
        return;
    debug_out("omap3_i2c_fifo_run s->tx_pos %x s->con %x stat %x \n", s->tx_pos,
              s->con, s->stat);
    if ((s->con >> 9) & 1)
    {                           /* TRX */
        while (ack & (s->tx_pos > 0))
        {
            debug_out("s->tx_pos %x value %x \n", s->tx_pos,
                      s->tx_fifo[s->tx_pos]);
            ack = (i2c_send(s->bus, s->tx_fifo[s->tx_pos - 1]) >= 0);
            s->tx_pos--;
            s->count_cur++;
        }
        
        if (s->tx_pos <= s->xtrsh)
        {
            s->stat |= 1 << 4;  /* XRDY */
            s->stat &= ~(1 << 14);
        }
        else if ((s->cnt - s->tx_pos) < s->xtrsh)
        {
            s->stat &= ~(1 << 4);       /* XRDY */
            s->stat |= 1 << 14;
            /*XDR*/ s->bufstat &= 0xffc0;
            s->bufstat |= s->cnt - s->tx_pos;
        }
        
        
        if (!s->tx_pos)
        {
            s->stat |= 1 << 2;  /* ARDY */
            s->con &= ~(1 << 10);       /* MST */
        }
        debug_out("s->stat %x \n", s->stat);
    }
    else
    /*RX*/
    {
        while (s->rx_pos < OMAP3_RX_FIFO_LEN)
        {
            s->rx_fifo[s->rx_pos] |= i2c_recv(s->bus);
            s->rx_pos++;
            s->count_cur++;
        }
        if (s->rx_pos > s->rtrsh)
        {
            s->stat |= 1 << 3;  /* RRDY */
        }
        else
            s->stat &= ~(1 << 3);       /* RRDY */
    }
    debug_out("s->count_cur  %x s->cnt %x \n", s->count_cur, s->cnt);
    if (s->count_cur == (s->cnt - 1))
    {
        s->count_cur = 0;
        if ((s->con >> 1) & 1)
        {                       /* STP */
            i2c_end_transfer(s->bus);
            s->con &= ~(1 << 1);        /* STP */
            s->tx_pos = 0;
        }
        else
        {
            s->stat |= 1 << 2;  /* ARDY */
            s->con &= ~(1 << 10);       /* MST */
        }
    }
    debug_out("after omap3_i2c_fifo_run s->tx_pos %x s->con %x stat %x \n",
              s->tx_pos, s->con, s->stat);
    
}

static void omap3_i2c_generate_stat2(struct omap3_i2c_s *s)
{
    
    debug_out("omap3_i2c_generate_stat  s->tx_pos %x s->con %x stat %x \n",
              s->tx_pos, s->con, s->stat);
    if (!(s->con & 0x8000))
        return;
    /* static uint32_t read_count = 0;
     if (read_count < 10)
     {
     read_count++;
     return;
     }
     else
     read_count = 0; */
    
    if ((s->con >> 9) & 0x1)
    {
        if (s->tx_pos <= s->xtrsh)
        {
            s->stat |= 1 << 4;  /* XRDY */
            s->stat &= ~(1 << 14);
        }
        else if ((s->cnt - s->tx_pos) < s->xtrsh)
        {
            s->stat &= ~(1 << 4);       /* XRDY */
            s->stat |= 1 << 14;
            /*XDR*/ s->bufstat &= 0xffc0;
            s->bufstat |= s->cnt - s->tx_pos;
        }
    }
    else
    {
        s->stat &= ~(1 << 4);   /* XRDY */
        s->stat &= ~(1 << 14);
    }
}

static void omap3_i2c_fifo_run2(struct omap3_i2c_s *s)
{
    debug_out
    ("omap3_i2c_fifo_run s->tx_pos %x s->con %x stat %x i2c_bus_busy %x \n",
     s->tx_pos, s->con, s->stat, i2c_bus_busy(s->bus));
    
    if (!i2c_bus_busy(s->bus))
        return;
    
    if ((s->con >> 9) & 1)
    {
        /*Transmit */
        while (s->tx_pos > 0)
        {
            i2c_send(s->bus, s->tx_fifo[s->tx_pos - 1]);
            s->tx_pos--;
            s->count_cur++;
        }
        if (!s->tx_pos)
        {
            s->con &= ~(1 << 10);       /* MST */
        }
        omap3_i2c_regenerate_stat(s);
    }
    else
    {
        /*TODO: Receive */
    }
    if (s->count_cur == s->cnt)
    {
        s->count_cur = 0;
        if ((s->con >> 1) & 1)
            omap3_i2c_stp_set(s);
    }
    
}

static void omap3_i2c_fifo_run(struct omap3_i2c_s *s)
{
    
    debug_out
    ("omap3_i2c_fifo_run s->tx_pos %x s->con %x stat %x i2c_bus_busy %x \n",
     s->tx_pos, s->con, s->stat, i2c_bus_busy(s->bus));
    
    if (!i2c_bus_busy(s->bus))
        return;
    if (!(s->con & 0x8000))
        return;
    
    if (s->con & (0x1 << 9))
    {
        /*TRX*/ while (s->tx_pos > 0)
        {
            i2c_send(s->bus, s->tx_fifo[s->tx_pos - 1]);
            s->tx_pos--;
            s->count_cur++;
        }
        if (s->tx_pos <= s->xtrsh)
        {
            if ((s->cnt - s->tx_pos) < s->xtrsh)
            {
                s->stat &= ~(1 << 4);   /* XRDY */
                s->stat |= 1 << 14;
            }
            else
            {
                s->stat |= 1 << 4;      /* XRDY */
                s->stat &= ~(1 << 14);
            }
            s->bufstat &= 0xffc0;
            s->bufstat |= s->cnt - s->tx_pos;
        }
#if 0
        if (s->tx_pos <= s->xtrsh)
        {
            s->stat |= 1 << 4;  /* XRDY */
            s->stat &= ~(1 << 14);
        }
        else if ((s->cnt - s->tx_pos) < s->xtrsh)
        {
            s->stat &= ~(1 << 4);       /* XRDY */
            s->stat |= 1 << 14;
            /*XDR*/ s->bufstat &= 0xffc0;
            s->bufstat |= s->cnt - s->tx_pos;
        }
#endif
        debug_out("s->count_cur %x s->cnt %x \n", s->count_cur, s->cnt);
        if (s->count_cur == s->cnt)
        {
            if ((s->con >> 1) & 0x1)
            {
                i2c_end_transfer(s->bus);
                s->con &= ~(1 << 1);    /* STP */
                s->tx_pos = 0;
                s->count_cur = 0;
                s->stat |= 1 << 2;   /*ARDY*/
            }
            /*clear XRDY if no data to send*/
            s->stat &= ~(1 << 4);
            s->stat &= ~(1 << 14);
            s->con &= ~(1 << 10);       /* MST */
        }
        
    }
    /*TODO:RECEIVE*/
    
}



static void omap3_i2c_txfifo_clr(struct omap3_i2c_s *s)
{
    memset(s->tx_fifo, 0x0, sizeof(s->tx_fifo));
    s->tx_pos = 0;
}

static void omap3_i2c_rxfifo_clr(struct omap3_i2c_s *s)
{
    memset(s->rx_fifo, 0x0, sizeof(s->rx_fifo));
    s->rx_pos = 0;
}


static void omap3_i2c_fifo_run55(struct omap3_i2c_s *s)
{
    
    if (!(s->con & 0x8000))
        return;
    
    /*transmit */
    if ((s->con >> 9) & 1)
    {
        if (i2c_bus_busy(s->bus))
        {
            while (s->tx_pos > 0)
            {
                i2c_send(s->bus, s->tx_fifo[s->tx_pos - 1]);
                s->tx_pos--;
                s->count_cur++;
            }
            if (s->tx_pos <= s->xtrsh)
            {
                s->stat |= 1 << 4;      /* XRDY */
                s->stat &= ~(1 << 14);
            }
            else if ((s->cnt - s->tx_pos) < s->xtrsh)
            {
                s->stat &= ~(1 << 4);   /* XRDY */
                s->stat |= 1 << 14;
                /*XDR*/ s->bufstat &= 0xffc0;
                s->bufstat |= s->cnt - s->tx_pos;
            }
            printf("s->tx_pos %x s->count_cur %x s->cnt %x \n", s->tx_pos,
                   s->count_cur, s->cnt);
            if (s->count_cur == s->cnt)
            {
                if ((s->con >> 1) & 0x1)
                {
                    i2c_end_transfer(s->bus);
                    s->con &= ~(1 << 1);        /* STP */
                    s->tx_pos = 0;
                    s->count_cur = 0;
                }
                s->stat |= 1 << 2;
                s->stat &= ~(1 << 4);
                s->stat &= ~(1 << 14);
                s->con &= ~(1 << 10);   /* MST */
            }
        }
    }
    else
    {
        s->stat &= ~(1 << 4);
        s->stat &= ~(1 << 14);
    }
    
    
}


static void omap3_i2c_reset(struct omap3_i2c_s *s)
{
    s->ie = 0;
    s->stat = 0;
    
    s->we = 0;
    s->syss = 0;
    s->buf = 0;
    s->cnt = 0;
    s->data = 0;
    s->sysc = 0;
    s->con = 0;
    s->oa0 = 0;
    s->sa = 0x3ff;
    s->psc = 0;
    s->scll = 0;
    s->sclh = 0;
    s->systest = 0;
    s->bufstat = 0;
    s->oa1 = 0;
    s->oa2 = 0;
    s->oa3 = 0;
    s->actoa = 0;
    s->sblock = 0;
    memset(s->tx_fifo, 0x0, sizeof(s->tx_fifo));
    memset(s->rx_fifo, 0x0, sizeof(s->rx_fifo));
    s->tx_pos = 0;
    s->rx_pos = 0;
    s->xtrsh = 1;
    s->rtrsh = 1;
    s->rdma_en = 0;
    s->xdma_en = 0;
    s->count_cur = 0;
    //s->start_condition = 0;
    if (i2c_bus_busy(s->bus))
        i2c_end_transfer(s->bus);
    
    
}

static uint32_t omap3_i2c_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_i2c_s *s = (struct omap3_i2c_s *) opaque;
    uint16_t ret;
    
    //printf("omap3_i2c_read offset %x pc %x \n", offset,
    //       cpu_single_env->regs[15]);
    switch (addr)
    {
        case 0x0:
            return s->rev;
        case 0x4:
            return s->ie;
        case 0x8:
            //omap3_i2c_generate_stat(s);
            debug_out("read s->state %x \n",
                      s->stat | (i2c_bus_busy(s->bus) << 12));
            return s->stat | (i2c_bus_busy(s->bus) << 12);
        case 0xc:
            return s->we;
        case 0x10:
            return s->syss | 0x1;
        case 0x14:
            return s->buf;
        case 0x18:
            return s->cnt;
        case 0x1c:
            printf("I2C read \n");
            debug_out("I2C read \n");
            ret = 0;
            ret |= s->rx_fifo[s->rx_pos] << 8;
            ret |= s->rx_fifo[s->rx_pos - 1] << 8;
            s->rx_pos -= 2;
            
            s->stat &= ~(1 << 11);  /* ROVR */
            
            if (s->rx_pos > s->rtrsh)
                s->stat |= (1 << 3);        /* RRDY */
            else
                s->stat &= ~(1 << 3);       /* RRDY */
            if (((s->con >> 10) & 1) &&     /* MST */
                ((~s->con >> 9) & 1))
            {                       /* TRX */
                s->stat |= 1 << 2;  /* ARDY */
                s->con &= ~(1 << 10);       /* MST */
            }
            omap3_i2c_fifo_run(s);
            omap3_i2c_interrupts_update(s);
            return ret;
            case 0x20:
            return s->sysc;
            case 0x24:
            return s->con;
            case 0x28:                 /* I2C_OA */
            return s->oa0;
            
            case 0x2c:                 /* I2C_SA */
            return s->sa;
            
            case 0x30:                 /* I2C_PSC */
            return s->psc;
            
            case 0x34:                 /* I2C_SCLL */
            return s->scll;
            
            case 0x38:                 /* I2C_SCLH */
            return s->sclh;
            
            case 0x3c:                 /* I2C_SYSTEST */
            if (s->systest & (1 << 15))
            {                       /* ST_EN */
                s->systest ^= 0xa;
                return s->systest;
            }
            else
                return s->systest & ~0x300f;
            case 0x40:
            return s->bufstat;
            case 0x44:
            return s->oa1;
            case 0x48:
            return s->oa2;
            case 0x4c:
            return s->oa3;
            case 0x50:
            return s->actoa;
            case 0x54:
            return s->sblock;
            
    }
    
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap3_i2c_write(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    struct omap3_i2c_s *s = (struct omap3_i2c_s *) opaque;
    int nack;
    
    debug_out("I2C Write offset %x value %x pc %x \n", offset, value,
              cpu_single_env->regs[15]);
    //printf("I2C Write offset %x value %x pc %x \n", offset, value,
    //          cpu_single_env->regs[15]);
    
    switch (addr)
    {
        case 0x0:
        case 0x10:
        case 0x40:
        case 0x50:
            OMAP_RO_REG(addr);
            return;
        case 0x4:
            s->ie = value & 0x63ff;
            break;
        case 0x8:
            s->stat &= ~(value & 0x63ff);
            omap3_i2c_fifo_run(s);
            omap3_i2c_interrupts_update(s);
            break;
        case 0xc:
            s->we = value & 0x6363;
            break;
        case 0x14:
            s->buf = value & 0xffff;
            s->xtrsh = (value & 0x3f) + 1;
            s->xdma_en = (value >> 7) & 0x1;
            s->rtrsh = ((value >> 8) & 0x3f) + 1;
            s->rdma_en = (value >> 15) & 0x1;
            if ((value >> 6) & 0x1)
                omap3_i2c_txfifo_clr(s);
            if ((value >> 14) & 0x1)
                omap3_i2c_rxfifo_clr(s);
            break;
        case 0x18:
            s->cnt = value & 0xffff;
            break;
        case 0x1c:
            /*TODO:Overflow */
            s->tx_fifo[s->tx_pos] = value & 0xff;
            s->tx_pos += 1;
            s->stat &= ~(1 << 10);  /* XUDF */
            omap3_i2c_fifo_run(s);
            omap3_i2c_interrupts_update(s);
            break;
        case 0x20:
            s->sysc &= 0x31f;
            break;
        case 0x24:
            s->con = value & 0xbff3;
            if (~value & (1 << 15))
            {                       /* I2C_EN */
                debug_out("reset i2c \n");
                omap3_i2c_reset(s);
                break;
            }
            
            /*Blow I2C_EN=1 */
            
            /*OPMODE*/
#if 0
            if (value & (0x3 << 12))
            {
                fprintf(stderr, "%s: I^2C only FS mode is supported\n",
                        __FUNCTION__);
                //break;
            }
#endif
            if (!(value & (1 << 10)))
            {                       /* MST */
                fprintf(stderr, "%s: I^2C slave mode not supported\n",
                        __FUNCTION__);
                //break;
            }
            if (value & (0x1f << 4))
            {
                fprintf(stderr,
                        "%s: 10-bit addressing mode not supported\n", __FUNCTION__);
                // break;
            }
            if (value & (1 << 9))
                omap3_i2c_txfifo_clr(s);
            else
                omap3_i2c_rxfifo_clr(s);
            if (value & 0x1)
            {
                /*STT*/ i2c_start_transfer(s->bus, s->sa, 0);
                s->count_cur = 0;
                s->con &= ~(1 << 0);
            }
            omap3_i2c_fifo_run(s);
            omap3_i2c_interrupts_update(s);
            
            break;
        case 0x28:
            s->oa0 = value & 0xe3ff;
            break;
        case 0x2c:
            s->sa = value & 0x3ff;
            break;
        case 0x30:
            s->psc = value & 0xff;
            break;
        case 0x34:
            s->scll = value & 0xffff;
            break;
        case 0x38:
            s->sclh = value & 0xffff;
            break;
        case 0x3c:
            s->systest = value & 0xf81f;
            if (value & (1 << 15))
                fprintf(stderr, "%s: I^2C systest mode not supported\n",
                        __FUNCTION__);
            exit(-1);
        case 0x44:
            s->oa1 = value & 0x3ff;
            break;
        case 0x48:
            s->oa2 = value & 0x3ff;
            break;
        case 0x4c:
            s->oa3 = value & 0x3ff;
            break;
        case 0x54:
            s->sblock = value & 0xf;
            break;
            
    }
    
}

static void omap3_i2c_writeb(void *opaque, target_phys_addr_t addr,
                             uint32_t value)
{
    struct omap3_i2c_s *s = (struct omap3_i2c_s *) opaque;
    
    debug_out("I2C Writeb addr %x value %x pc %x \n", addr, value,
              cpu_single_env->regs[15]);
    
    switch (addr)
    {
        case 0x1c:                 /* I2C_DATA */
            omap3_i2c_write(opaque,addr,value);
            break;
            
        default:
            OMAP_BAD_REG(addr);
            return;
    }
}

static CPUReadMemoryFunc *omap3_i2c_readfn[] = {
    omap_badwidth_read16,
    omap3_i2c_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap3_i2c_writefn[] = {
    omap3_i2c_writeb,           /* Only the last fifo write can be 8 bit.  */
    omap3_i2c_write,
    omap_badwidth_write16,
};

struct omap3_i2c_s *omap3_i2c_init(struct omap_target_agent_s *ta,
                                   qemu_irq irq, qemu_irq * dma,
                                   omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap3_i2c_s *s = (struct omap3_i2c_s *)
    qemu_mallocz(sizeof(struct omap3_i2c_s));
    
    s->rev = 0x3c;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    //s->slave.event = omap_i2c_event;
    //s->slave.recv = omap_i2c_rx;
    //s->slave.send = omap_i2c_tx;
    s->bus = i2c_init_bus();
    omap3_i2c_reset(s);
    
    iomemtype = l4_register_io_memory(0, omap3_i2c_readfn,
                                      omap3_i2c_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    
    debug_init();
    
    return s;
}


i2c_bus *omap3_i2c_bus(struct omap3_i2c_s * s)
{
    return s->bus;
}
