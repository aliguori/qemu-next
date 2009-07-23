/*
 * Extended boot option ROM support.
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw.h"
#include "pc.h"
#include "isa.h"
#include "block.h"

/* Extended Boot ROM suport */

#define EXTBOOT_QUERY_GEOMETRY 0x00
#define EXTBOOT_XFER_IN        0x02
#define EXTBOOT_XFER_OUT       0x02

static void get_translated_chs(BlockDriverState *bs, int *c, int *h, int *s)
{
    bdrv_get_geometry_hint(bs, c, h, s);

    if (*c <= 1024) {
	*c >>= 0;
	*h <<= 0;
    } else if (*c <= 2048) {
	*c >>= 1;
	*h <<= 1;
    } else if (*c <= 4096) {
	*c >>= 2;
	*h <<= 2;
    } else if (*c <= 8192) {
	*c >>= 3;
	*h <<= 3;
    } else {
	*c >>= 4;
	*h <<= 4;
    }

    /* what is the correct algorithm for this?? */
    if (*h == 256) {
	*h = 255;
	*c = *c + 1;
    }
}

static void extboot_query_geometry(BlockDriverState *bs, int *pcylinders,
                                   int *pheads, int *psectors,
                                   uint64_t *pnb_sectors)
{
    int cylinders, heads, sectors, err;
    uint64_t nb_sectors;

    get_translated_chs(bs, pcylinders, pheads, psectors);
    bdrv_get_geometry(bs, pnb_sectors);
}

static void extboot_write_cmd(void *opaque, uint32_t addr, uint32_t value)
{
    BlockDriverState *bs = opaque;
    target_phys_addr_t cmd_addr;
    target_phys_addr_t pa = 0;
    int blen = 0;
    void *buf = NULL;

    cmd_addr = (value & 0xFFFF);

    type = lduw_phys(cmd_addr);
    switch (type) {
    case EXTBOOT_QUERY_GEOMETRY:
        extboot_query_geometry(bs, &cylinders, &heads, &sectors, &nb_sectors);

        stw_phys(cmd_addr + 2, cylinders);
        stw_phys(cmd_addr + 4, heads);
        stw_phys(cmd_addr + 6, sectors);
        stq_phys(cmd_addr + 8, nb_sectors);
        break;
    case EXTBOOT_XFER_IN:
    case EXTBOOT_XFER_OUT:
        nb_sectors = lduw(cmd_addr + 2);
        segment = lduw(cmd_addr + 4);
        offset = lduw(cmd_addr + 6);
        sector = ldq(cmd_addr + 8);

        extboot_xfer(bs, !!(type == EXTBOOT_XFER_IN), nb_sectors, segment, offset, sector);

        break;
    }

    switch (cmd.type) {
    case 0x00:
	cmd.query_geometry.cylinders = cylinders;
	cmd.query_geometry.heads = heads;
	cmd.query_geometry.sectors = sectors;
	cmd.query_geometry.nb_sectors = nb_sectors;
	break;
    case 0x01:
	err = bdrv_read(bs, cmd.xfer.sector, buf, cmd.xfer.nb_sectors);
	if (err)
	    printf("Read failed\n");

        cpu_physical_memory_write(pa, buf, blen);

	break;
    case 0x02:
        cpu_physical_memory_read(pa, buf, blen);

	err = bdrv_write(bs, cmd.xfer.sector, buf, cmd.xfer.nb_sectors);
	if (err)
	    printf("Write failed\n");

	break;
    }

    cpu_physical_memory_write((value & 0xFFFF) << 4, (uint8_t *)&cmd,
                              sizeof(cmd));
    if (buf)
        qemu_free(buf);
}

void extboot_init(BlockDriverState *bs)
{
    register_ioport_write(0x405, 1, 2, extboot_write_cmd, bs);
}
