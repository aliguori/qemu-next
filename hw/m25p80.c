/*
 * ST M25P80 emulator.
 *
 * Copyright (C) 2011 Edgar E. Iglesias <edgar.iglesias@gmail.com>
 * Copyright (C) 2012 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>
 * Copyright (C) 2012 PetaLogix
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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "blockdev.h"
#include "ssi.h"
#include "devices.h"

#ifdef M25P80_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

enum FlashCMD {
    NOP = 0,
    PP = 0x2,
    READ = 0x3,
    WRDI = 0x4,
    RDSR = 0x5,
    WREN = 0x6,
    FAST_READ = 0xb,
    SECTOR_ERASE = 0x20,
    BLOCK_ERASE32 = 0x52,
    JEDEC_READ = 0x9f,
    CHIP_ERASE = 0xc7,
};

enum CMDState {
    STATE_IDLE,
    STATE_PAGE_PROGRAM,
    STATE_READ,
    STATE_COLLECTING_DATA,
    STATE_READING_DATA,
};

struct flash {
    SSISlave ssidev;
    uint32_t r;

    BlockDriverState *bdrv;
    enum CMDState state;

    uint8_t *storage;
    uint64_t size;
    int pagesize;
    int sectorsize;
    int blocksize;

    uint8_t data[16];
    int len;
    int pos;
    int wrap_read;
    int needed_bytes;
    enum FlashCMD cmd_in_progress;

    int64_t dirty_page;

    uint64_t waddr;
    int write_enable;
};

static void bdrv_sync_complete(void *opaque, int ret)
{

}

static void flash_sync_page(struct flash *s, int page)
{
    if (s->bdrv) {
        int bdrv_sector, nb_sectors;
        QEMUIOVector iov;

        bdrv_sector = (page * s->pagesize) / 512;
        nb_sectors = (s->pagesize + 511) / 512;
        qemu_iovec_init(&iov, 1);
        qemu_iovec_add(&iov, s->storage + bdrv_sector * 512,
                                                    nb_sectors * 512);
        bdrv_aio_writev(s->bdrv, bdrv_sector, &iov, nb_sectors,
                                                bdrv_sync_complete, NULL);
    }
}

static inline void flash_sync_area(struct flash *s, int64_t off, int64_t len)
{
    int64_t start, end;

    if (!s->bdrv) {
        return;
    }

    start = off / 512;
    end = (off + len) / 512;
    bdrv_write(s->bdrv, start, s->storage + (start * 512), end - start);
}

static void flash_sector_erase(struct flash *s, int sector)
{
    if (!s->write_enable) {
        DB_PRINT("write with write protect!\n");
    }
    memset(s->storage + sector, 0xff, s->sectorsize);
    flash_sync_area(s, sector, s->sectorsize);
}

static void flash_block_erase32k(struct flash *s, int addr)
{
    if (!s->write_enable) {
        DB_PRINT("write with write protect!\n");
    }
    memset(s->storage + addr, 0xff, 32 * 1024);
    flash_sync_area(s, addr, 32 * 1024);
}

static void flash_chip_erase(struct flash *s)
{
    if (!s->write_enable) {
        DB_PRINT("write with write protect!\n");
    }
    memset(s->storage, 0xff, s->size);
    flash_sync_area(s, 0, s->size);
}

static inline void flash_sync_dirty(struct flash *s, int64_t newpage)
{
    if (s->dirty_page >= 0 && s->dirty_page != newpage) {
        flash_sync_page(s, s->dirty_page);
        s->dirty_page = newpage;
    }
}

static inline
void flash_write8(struct flash *s, uint64_t addr, uint8_t data)
{
    int64_t page = addr / s->pagesize;
    uint8_t prev = s->storage[s->waddr];

    if (!s->write_enable) {
        DB_PRINT("write with write protect!\n");
    }

    if ((prev ^ data) & data) {
        DB_PRINT("programming zero to one! addr=%lx  %x -> %x\n",
                  addr, prev, data);
    }
    s->storage[s->waddr] ^= ~data & s->storage[s->waddr];

    flash_sync_dirty(s, page);
    s->dirty_page = page;
}

static void complete_collecting_data(struct flash *s)
{
    s->waddr = s->data[0] << 16;
    s->waddr |= s->data[1] << 8;
    s->waddr |= s->data[2];

    switch (s->cmd_in_progress) {
    case PP:
        s->state = STATE_PAGE_PROGRAM;
        break;
    case READ:
    case FAST_READ:
        s->state = STATE_READ;
        break;
    case SECTOR_ERASE:
        DB_PRINT("sector_erase sector=%x\n", (unsigned)s->waddr);
        flash_sector_erase(s, s->waddr);
        break;
    case BLOCK_ERASE32:
        DB_PRINT("block_erase addr=%x\n", (unsigned)s->waddr);
        flash_block_erase32k(s, s->waddr);
        break;
    default:
        break;
    }
}

static void decode_new_cmd(struct flash *s, uint32_t value)
{
    s->cmd_in_progress = value;
    DB_PRINT("decoded new command:%d\n", value);

    switch (value) {

    case SECTOR_ERASE:
    case BLOCK_ERASE32:
    case READ:
    case PP:
        s->needed_bytes = 3;
        s->pos = 0; s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;
    case FAST_READ:
        s->needed_bytes = 4;
        s->pos = 0; s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;

    case WRDI:
        s->write_enable = 0;
        break;
    case WREN:
        s->write_enable = 1;
        break;

    case RDSR:
        s->data[0] = (!!s->write_enable) << 1;
        s->pos = 0; s->len = 1; s->wrap_read = 0;
        s->state = STATE_READING_DATA;
        break;

    case JEDEC_READ:
        DB_PRINT("populated jedec code\n");
        s->data[0] = 0xef;
        s->data[1] = 0x40;
        s->data[2] = 0x17;
        s->pos = 0;
        s->len = 3;
        s->wrap_read = 0;
        s->state = STATE_READING_DATA;
        break;

    case CHIP_ERASE:
        if (s->write_enable) {
            DB_PRINT("chip erase\n");
            flash_chip_erase(s);
        } else {
            DB_PRINT("chip erase with write protect!\n");
        }
        break;
    case NOP:
        break;
    default:
        DB_PRINT("Unknown cmd %x\n", value);
        break;
    }
}

static int m25p80_cs(SSISlave *ss, int select)
{
    struct flash *s = FROM_SSI_SLAVE(struct flash, ss);

    if (!select) {
        s->len = 0;
        s->pos = 0;
        s->state = STATE_IDLE;
        flash_sync_dirty(s, -1);
        DB_PRINT("deselect\n");
    }

    return 0;
}

static uint32_t m25p80_transfer8(SSISlave *ss, uint32_t tx)
{
    struct flash *s = FROM_SSI_SLAVE(struct flash, ss);
    uint32_t r = 0;

    switch (s->state) {

    case STATE_PAGE_PROGRAM:
        DB_PRINT("page program waddr=%lx data=%x\n", s->waddr, (uint8_t)tx);
        flash_write8(s, s->waddr, (uint8_t)tx);
        s->waddr++;
        break;

    case STATE_READ:
        r = s->storage[s->waddr];
        DB_PRINT("READ 0x%lx=%x\n", s->waddr, r);
        s->waddr = (s->waddr + 1) % s->size;
        break;

    case STATE_COLLECTING_DATA:
        s->data[s->len] = (uint8_t)tx;
        s->len++;

        if (s->len == s->needed_bytes) {
            complete_collecting_data(s);
        }
        break;

    case STATE_READING_DATA:
        r = s->data[s->pos];
        s->pos++;
        if (s->pos == s->len) {
            s->pos = 0;
            if (!s->wrap_read) {
                s->state = STATE_IDLE;
            }
        }
        break;

    default:
    case STATE_IDLE:
        decode_new_cmd(s, (uint8_t)tx);
        break;
    }

    return r;
}

static int m25p80_init(SSISlave *ss)
{
    DriveInfo *dinfo;
    struct flash *s = FROM_SSI_SLAVE(struct flash, ss);
    static int mtdblock_idx;
    dinfo = drive_get(IF_MTD, 0, mtdblock_idx++);

    DB_PRINT("inited m25p80 device model - dinfo = %p\n", dinfo);
    /* TODO: parameterize */
    s->size = 8 * 1024 * 1024;
    s->pagesize = 256;
    s->sectorsize = 4 * 1024;
    s->dirty_page = -1;
    s->storage = qemu_blockalign(s->bdrv, s->size);

    if (dinfo && dinfo->bdrv) {
        int rsize;

        s->bdrv = dinfo->bdrv;
        rsize = MIN(bdrv_getlength(s->bdrv), s->size);
        if (bdrv_read(s->bdrv, 0, s->storage, (s->size + 511) / 512)) {
            fprintf(stderr, "Failed to initialize SPI flash!\n");
            return 1;
        }
    } else {
        s->write_enable = 1;
        flash_chip_erase(s);
        s->write_enable = 0;
    }

    return 0;
}

static void m25p80_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = m25p80_init;
    k->transfer = m25p80_transfer8;
    k->set_cs = m25p80_cs;
}

static TypeInfo m25p80_info = {
    .name           = "m25p80",
    .parent         = TYPE_SSI_SLAVE,
    .instance_size  = sizeof(struct flash),
    .class_init     = m25p80_class_init,
};

static void m25p80_register_types(void)
{
    type_register_static(&m25p80_info);
}

type_init(m25p80_register_types)
