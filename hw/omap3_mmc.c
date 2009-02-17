/*
 * OMAP3 Multimedia Card/Secure Digital/Secure Digital I/O (MMC/SD/SDIO) Card Interface emulation
 *
 * Copyright (C) 2008 yajin  <yajin@vm-kernel.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*The MMCHS of OMAP3530/3430 is different from OMAP1 and OAMP2420.*/


#include "hw.h"
#include "omap.h"
#include "sd.h"


//#define MMC_DEBUG_

#ifdef MMC_DEBUG_
#define TRACE(fmt,...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

struct omap3_mmc_s
{
    qemu_irq irq;
    qemu_irq *dma;
    qemu_irq coverswitch;
    omap_clk clk;
    SDState *card;


    uint32_t sysconfig;         /*0x10 */
    uint32_t sysstatus;         /*0x14 */
    uint32_t csre;              /*0x24 */
    uint32_t systest;           /*0x28 */
    uint32_t con;               /*0x2c */
    uint32_t pwcnt;             /*0x30 */
    uint32_t blk;               /*0x104 */
    uint32_t arg;               /*0x108 */
    uint32_t cmd;               /*0x10c */
    uint32_t rsp10;             /*0x110 */
    uint32_t rsp32;             /*0x114 */
    uint32_t rsp54;             /*0x118 */
    uint32_t rsp76;             /*0x11c */
    uint32_t data;              /*0x120 */
    uint32_t pstate;            /*0x124 */
    uint32_t hctl;              /*0x128 */
    uint32_t sysctl;            /*0x12c */
    uint32_t stat;              /*0x130 */
    uint32_t ie;                /*0x134 */
    uint32_t ise;               /*0x138 */
    uint32_t ac12;              /*0x13c */
    uint32_t capa;              /*0x140 */
    uint32_t cur_capa;          /*0x148 */
    uint32_t rev;               /*0x1fc */

    /*for quick reference */
    uint16_t blen_counter;
    uint16_t nblk_counter;

    uint32_t fifo[256];
    int fifo_start;
    int fifo_len;

    int ddir;
    int transfer;
    
    uint32_t stat_pending;
};


typedef enum
{
    sd_nore = 0,                /* no response */
    sd_136_bits = 1,            /*Response Length 136 bits */
    sd_48_bits = 2,             /*Response Length 48 bits */
    sd_48b_bits = 3,            /*Response Length 48 bits with busy after response */
} omap3_sd_rsp_type_t;

int test=1;
static void omap3_mmc_interrupts_update(struct omap3_mmc_s *s)
{
    qemu_set_irq(s->irq, !!((s->stat | s->stat_pending) & s->ise & s->ie));
}

static void omap3_mmc_fifolevel_update(struct omap3_mmc_s *host,int de)
{
	if (!de)
		return;

	if (host->ddir)
	{
		/*card -> host*/
		if (host->fifo_len<(host->blk&0x7f))
		{
			/*DMA has read one byte from fifo*/
			 qemu_irq_lower(host->dma[1]);
		}
	}
	else
	{
		if (host->fifo_len>0)
		{
			/*DMA has written one byte to fifo*/
			qemu_irq_lower(host->dma[0]);
		}
	}

}

static void omap3_mmc_transfer(struct omap3_mmc_s *host, int msbs, int ace,
                               int bce, int de)
{
    uint8_t value;
    int i;

    if (!host->transfer)
        return;
    while (1)
    {
        if (host->ddir)
        {
            /*data read. card->host */
            for (i = 0; i < 4; i++)
            {
                if (host->blen_counter)
                {
                   value = sd_read_data(host->card);
                   //if (host->arg==0x13c00)
                   //	printf("value %x ",value);
                    host->fifo[(host->fifo_start + host->fifo_len) & 255] |=
                        value << (i * 8);
                    host->blen_counter--;
                }
                else
                    break;
            }
            host->fifo_len++;
        }
        else
        {
            /*data write. host->card */
            if (!host->fifo_len)
                break;
            for (i = 0; i < 4; i++)
            {
                if (host->blen_counter)
                {
                    value = (host->fifo[host->fifo_start] >> (i * 8)) & 0xff;
                    sd_write_data(host->card, value);
                    host->blen_counter--;
                }
                else
                    break;
            }
            host->fifo_start++;
            host->fifo_len--;
            host->fifo_start &= 255;
        }

        if (host->blen_counter == 0)
        {
            host->nblk_counter--;
            host->blen_counter = host->blk & 0x7ff;
            if (msbs)
            {
                /*multi block transfer */
                if (host->nblk_counter == 0)
                {
                    host->nblk_counter = (host->blk >> 16) & 0xffff;
                    host->transfer = 0;
                    host->stat_pending |= 0x2;        /*tc */
                    break;
                }
            }
            else
            {
            	  /*single block transfer*/
                host->transfer = 0;
                host->stat_pending |= 0x2;    /*tc */
                break;
            }
        }

    }

    /*transfer complete*/
    if (de)
    {
        /*DMA*/
    	if (host->ddir)
    	{
    		/*card->host*/
          qemu_irq_raise(host->dma[1]);
    	}
    	else
    	{
    		qemu_irq_raise(host->dma[0]);
    	}
    	/*clear BRR BWR*/
    	host->stat &= ~0x30;
        host->stat_pending &= ~0x30;
    }
   else
    {
    	/*not DMA*/
    	if (host->ddir)
    	{
    		host->pstate |= 0x800;  /*BRE*/
    		host->pstate &= ~0x400;  /*BWE*/  /*can not write*/
    		host->stat_pending |= 0x20;  /*BRR*/
    		host->stat &= ~0x10; /*BWR*/
            host->stat_pending &= ~0x10;
    	}
    	else
    	{
    		host->pstate &= ~0x800;  /*BRE*/
    		host->pstate |= 0x400;  /*BWE*/
    		host->stat_pending |= 0x10;  /*BWR*/
    		host->stat &= ~0x20; /*BRR*/
            host->stat_pending &= ~0x20;
    	}
    		
    }

   	//printf("after MMC TRANS host->stat %x \n",host->stat);

   
}

static void omap3_mmc_command(struct omap3_mmc_s *host, int indx, int dp,
                              omap3_sd_rsp_type_t rsp_type, int ddir)
{
    uint32_t rspstatus, mask;
    int rsplen, timeout;
    struct sd_request_s request;
    uint8_t response[16];

    //printf("CMD %d host->arg %x \n",indx,host->arg);

    if ((host->con & 2) && !indx) /* INIT and CMD0 */
    {
        host->stat_pending |= 0x1;
        host->pstate &= 0xfffffffe;
        return;
    }

    if (dp)
    {
        host->fifo_start = 0;
        host->fifo_len = 0;
        host->transfer = 1;
        host->ddir = ddir;
    }
    else
        host->transfer = 0;

    timeout = 0;
    mask = 0;
    rspstatus = 0;

    request.cmd = indx;
    request.arg = host->arg;
    request.crc = 0;            /* FIXME */

    rsplen = sd_do_command(host->card, &request, response);

    switch (rsp_type)
    {
    case sd_nore:
        rsplen = 0;
        break;
    case sd_136_bits:
        if (rsplen < 16)
        {
            timeout = 1;
            break;
        }
        rsplen = 16;
        host->rsp76 = (response[0] << 24) | (response[1] << 16) |
            (response[2] << 8) | (response[3] << 0);
        host->rsp54 = (response[4] << 24) | (response[5] << 16) |
            (response[6] << 8) | (response[7] << 0);
        host->rsp32 = (response[8] << 24) | (response[9] << 16) |
            (response[10] << 8) | (response[11] << 0);
        host->rsp10 = (response[12] << 24) | (response[13] << 16) |
            (response[14] << 8) | (response[15] << 0);
        break;
    case sd_48_bits:
    case sd_48b_bits:
        if (rsplen < 4)
        {
            timeout = 1;
            break;
        }
        rsplen = 4;
        host->rsp10 = (response[0] << 24) | (response[1] << 16) |
            (response[2] << 8) | (response[3] << 0);
        switch (indx)
        {
        case 41:               /*r3 */
        case 8:                /*r7 */
        case 6:                /*r6 */
            break;
        default:
            mask = OUT_OF_RANGE | ADDRESS_ERROR | BLOCK_LEN_ERROR |
                ERASE_SEQ_ERROR | ERASE_PARAM | WP_VIOLATION |
                LOCK_UNLOCK_FAILED | COM_CRC_ERROR | ILLEGAL_COMMAND |
                CARD_ECC_FAILED | CC_ERROR | SD_ERROR |
                CID_CSD_OVERWRITE | WP_ERASE_SKIP;
            rspstatus = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) | (response[3] << 0);

            break;

        }

    }

    if (rspstatus & mask & host->csre)
        host->stat_pending |= 0x10000000;
    else {
        host->stat &= ~0x10000000;
        host->stat_pending &= ~0x10000000;
    }

    if (timeout)
        host->stat_pending |= 0x10000;
    else
        host->stat_pending |= 0x1;

    /*do we allow to set the stat bit? */
    host->stat_pending &= host->ie;

    if (host->stat_pending & 0xffff0000)
        host->stat_pending |= 0x8000;
}

static void omap3_mmc_reset(struct omap3_mmc_s *s)
{
    s->sysconfig = 0x00000015;
    s->con       = 0x00000500;
    s->pstate    = 0x00040000;
    s->capa      = 0x00e10080;
    s->rev       = 0x26000000;

    s->fifo_start =0;
    s->fifo_len =0;
}

static uint32_t omap3_mmc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_mmc_s *s = (struct omap3_mmc_s *) opaque;
    uint32_t i ;

    switch (addr) {
        case 0x10:
            TRACE("SYSCONFIG = %08x", s->sysconfig);
            return s->sysconfig;
        case 0x14:
            TRACE("SYSSTATUS = %08x", s->sysstatus | 0x1);
            return s->sysstatus | 0x1; /*reset completed */
        case 0x24:
            TRACE("CSRE = %08x", s->csre);
            return s->csre;
        case 0x28:
            TRACE("SYSTEST = %08x", s->systest);
            return s->systest;
        case 0x2c: /* MMCHS_CON */
            TRACE("CON = %08x", s->con);
            return s->con;
        case 0x30:
            TRACE("PWCNT = %08x", s->pwcnt);
            return s->pwcnt;
        case 0x104: /* MMCHS_BLK */
            TRACE("BLK = %08x", s->blk);
            return s->blk;
        case 0x108: /* MMCHS_ARG */
            TRACE("ARG = %08x", s->arg);
            return s->arg;
        case 0x10c:
            TRACE("CMD = %08x", s->cmd);
            return s->cmd;
        case 0x110:
            TRACE("RSP10 = %08x", s->rsp10);
            return s->rsp10;
        case 0x114:
            TRACE("RSP32 = %08x", s->rsp32);
            return s->rsp32;
        case 0x118:
            TRACE("RSP54 = %08x", s->rsp54);
            return s->rsp54;
        case 0x11c:
            TRACE("RSP76 = %08x", s->rsp76);
            return s->rsp76;
        case 0x120:
            /*Read Data */
            i = s->fifo[s->fifo_start];
            /*set the buffer to default value*/
            s->fifo[s->fifo_start] = 0x0;
            if (s->fifo_len == 0) {
                printf("MMC: FIFO underrun\n");
                return i;
            }
            s->fifo_start++;
            s->fifo_len--;
            s->fifo_start &= 255;
            omap3_mmc_transfer(s,(s->cmd>>5)&1,(s->cmd>>2)&1,(s->cmd>>1)&1,(s->cmd)&1);
            omap3_mmc_fifolevel_update(s,s->cmd&1);
            omap3_mmc_interrupts_update(s);
            return i;
        case 0x124: /* MMCHS_PSTATE */
            TRACE("PSTATE = %08x", s->pstate);
            return s->pstate;
        case 0x128:
            TRACE("HCTL = %08x", s->hctl);
            return s->hctl;
        case 0x12c: /* MMCHS_SYSCTL */
            TRACE("SYSCTL = %08x", s->sysctl);
            return s->sysctl;
        case 0x130: /* MMCHS_STAT */
            s->stat |= s->stat_pending;
            s->stat_pending = 0;
            TRACE("STAT = %08x", s->stat);
            return s->stat;
        case 0x134:
            TRACE("IE = %08x", s->ie);
            return s->ie;
        case 0x138:
            TRACE("ISE = %08x", s->ise);
            return s->ise;
        case 0x13c:
            TRACE("AC12 = %08x", s->ac12);
            return s->ac12;
        case 0x140: /* MMCHS_CAPA */
            TRACE("CAPA = %08x", s->capa);
            return s->capa;
        case 0x148:
            TRACE("CUR_CAPA = %08x", s->cur_capa);
            return s->cur_capa;
        case 0x1fc:
            TRACE("REV = %08x", s->rev);
            return s->rev;
        default:
            OMAP_BAD_REG(addr);
            exit(-1);
            return 0;
    }
}

static void omap3_mmc_write(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    struct omap3_mmc_s *s = (struct omap3_mmc_s *) opaque;
    
    switch (addr) {
        case 0x014:
        case 0x110:
        case 0x114:
        case 0x118:
        case 0x11c:
        case 0x124:
        case 0x13c:
        case 0x1fc:
            OMAP_RO_REG(addr);
            break;
        case 0x010:
            TRACE("SYSCONFIG = %08x", value);
            if (value & 2)
                omap3_mmc_reset(s);
            s->sysconfig = value & 0x31d;
            break;
        case 0x024:
            TRACE("CSRE = %08x", value);
            s->csre = value;
            break;
        case 0x028:
            TRACE("SYSTEST = %08x", value);
            s->systest = value;
            break;
        case 0x02c: /* MMCHS_CON */
            TRACE("CON = %08x", value);
            if (value & 0x10) {
                fprintf(stderr, "%s: SYSTEST mode is not supported\n", __FUNCTION__);
                exit(-1);
            }
            if (value & 0x20) {
                fprintf(stderr, "%s: 8-bit data width is not supported\n", __FUNCTION__);
                exit(-1);
            }
            s->con = value & 0x1ffff;
            break;
        case 0x030:
            TRACE("PWCNT = %08x", value);
            s->pwcnt = value;
            break;
        case 0x104: /* MMCHS_BLK */
            TRACE("BLK = %08x", value);
            s->blk = value & 0xffff07ff;
            s->blen_counter = value & 0x7ff;
            s->nblk_counter = (value & 0xffff) >> 16;
            break;
        case 0x108: /* MMCHS_ARG */
            TRACE("ARG = %08x", value);
            s->arg = value;
            break;
        case 0x10c: /* MMCHS_CMD */
            TRACE("CMD = %08x", value);
            s->cmd = value & 0x3ffb0037;
            omap3_mmc_command(s, (value >> 24) & 0x3f, (value >> 21) & 1,
                              (value >> 16) & 3, (value >> 4) & 1);
            omap3_mmc_transfer(s,(s->cmd>>5)&1,(s->cmd>>2)&1,(s->cmd>>1)&1,(s->cmd)&1);
            omap3_mmc_fifolevel_update(s,s->cmd&0x1);
            omap3_mmc_interrupts_update(s);
            break;
        case 0x120:
            /*data */
            if (s->fifo_len == 256)
                break;
            s->fifo[(s->fifo_start + s->fifo_len) & 255] = value;
            s->fifo_len ++;
            omap3_mmc_transfer(s,(s->cmd>>5)&1,(s->cmd>>2)&1,(s->cmd>>1)&1,(s->cmd)&1);
            omap3_mmc_fifolevel_update(s,s->cmd&0x1);
            omap3_mmc_interrupts_update(s);
            break;
        case 0x128: /* MMCHS_HCTL */
            TRACE("HCTL = %08x", value);
            s->hctl = value & 0xf0f0f02;
            break;
        case 0x12c: /* MMCHS_SYSCTL */
            TRACE("SYSCTL = %08x", value);
            if (value & 0x04000000) { /* SRD */
                s->data    = 0;
                s->pstate &= ~0x00000f06; /* BRE, BWE, RTA, WTA, DLA, DATI */
                s->hctl   &= ~0x00030000; /* SGBR, CR */
                s->stat   &= ~0x00000034; /* BRR, BWR, BGE */
                s->stat_pending &= ~0x00000034;
                s->fifo_start = 0;
                s->fifo_len = 0;
            }
            if (value & 0x02000000) { /* SRC */
                s->pstate &= ~0x00000001; /* CMDI */
            }
            if (value & 0x01000000) { /* SRA */
                uint32_t capa = s->capa;
                uint32_t cur_capa = s->cur_capa;
                omap3_mmc_reset(s);
                s->capa = capa;
                s->cur_capa = cur_capa;
            }
            value = (value & ~2) | ((value & 1) << 1); /* copy ICE directly to ICS */
            s->sysctl = value & 0x000fffc7;
            break;
        case 0x130:
            TRACE("STAT = %08x", value);
            value = value & 0x317f0237;
            s->stat &= ~value;
            /* stat_pending is NOT cleared */
            omap3_mmc_interrupts_update(s);
            break;
        case 0x134: /* MMCHS_IE */
            TRACE("IE = %08x", value);
            if (!(s->con & 0x4000)) /* if CON:OBIE is clear, ignore write to OBI_ENABLE */
                value = (value & ~0x200) | (s->ie & 0x200);
            s->ie = value & 0x317f0337;
            if (!(s->ie & 0x100)) {
                s->stat &= ~0x100;
                s->stat_pending &= ~0x100;
            }
            omap3_mmc_interrupts_update(s);
            break;
        case 0x138:
            TRACE("ISE = %08x", value);
            s->ise = value & 0x317f0337;
            omap3_mmc_interrupts_update(s);
            break;
        case 0x140: /* MMCHS_CAPA */
            TRACE("CAPA = %08x", value);
            s->capa &= ~0x07000000;
            s->capa |= value & 0x07000000;
            break;
        case 0x148:
            TRACE("CUR_CAPA = %08x", value);
            s->cur_capa = value & 0xffffff;
            break;
        default:
            OMAP_BAD_REG(addr);
            exit(-1);
    }
}

static CPUReadMemoryFunc *omap3_mmc_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_mmc_read,
};

static CPUWriteMemoryFunc *omap3_mmc_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_mmc_write,
};

static void omap3_mmc_enable(struct omap3_mmc_s *s, int enable)
{
    sd_enable(s->card, enable);
}

struct omap3_mmc_s *omap3_mmc_init(struct omap_target_agent_s *ta,
                                   BlockDriverState * bd, qemu_irq irq,
                                   qemu_irq dma[], omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap3_mmc_s *s = (struct omap3_mmc_s *)
        qemu_mallocz(sizeof(struct omap3_mmc_s));

    s->irq = irq;
    s->dma = dma;
    s->clk = fclk;

    omap3_mmc_reset(s);

    iomemtype = l4_register_io_memory(0, omap3_mmc_readfn,
                                      omap3_mmc_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    /* Instantiate the storage */
    if (bd!=NULL) {
    	s->card = sd_init(bd, 0);
	    omap3_mmc_enable(s,1);
    }

    return s;
}




