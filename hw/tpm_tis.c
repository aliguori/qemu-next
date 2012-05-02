/*
 * tpm_tis.c - QEMU's TPM TIS interface emulator
 *
 * Copyright (C) 2006,2010,2011 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * Xen 4 support: Andrease Niederl <andreas.niederl@iaik.tugraz.at>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputiggroup.org.
 * In the developers menu choose the PC Client section then find the TIS
 * specification.
 */

#include "tpm.h"
#include "block.h"
#include "exec-memory.h"
#include "hw/hw.h"
#include "hw/pc.h"
#include "hw/pci_ids.h"
#include "hw/tpm_tis.h"
#include "qemu-error.h"
#include "qemu-common.h"

/*#define DEBUG_TIS */

#ifdef DEBUG_TIS
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

/* whether the STS interrupt is supported */
/*#define RAISE_STS_IRQ */

/* tis registers */
#define TPM_TIS_REG_ACCESS                0x00
#define TPM_TIS_REG_INT_ENABLE            0x08
#define TPM_TIS_REG_INT_VECTOR            0x0c
#define TPM_TIS_REG_INT_STATUS            0x10
#define TPM_TIS_REG_INTF_CAPABILITY       0x14
#define TPM_TIS_REG_STS                   0x18
#define TPM_TIS_REG_DATA_FIFO             0x24
#define TPM_TIS_REG_DID_VID               0xf00
#define TPM_TIS_REG_RID                   0xf04

/* vendor-specific registers */
#define TPM_TIS_REG_DEBUG                 0xf90

#define TPM_TIS_STS_VALID                 (1 << 7)
#define TPM_TIS_STS_COMMAND_READY         (1 << 6)
#define TPM_TIS_STS_TPM_GO                (1 << 5)
#define TPM_TIS_STS_DATA_AVAILABLE        (1 << 4)
#define TPM_TIS_STS_EXPECT                (1 << 3)
#define TPM_TIS_STS_RESPONSE_RETRY        (1 << 1)

#define TPM_TIS_ACCESS_TPM_REG_VALID_STS  (1 << 7)
#define TPM_TIS_ACCESS_ACTIVE_LOCALITY    (1 << 5)
#define TPM_TIS_ACCESS_BEEN_SEIZED        (1 << 4)
#define TPM_TIS_ACCESS_SEIZE              (1 << 3)
#define TPM_TIS_ACCESS_PENDING_REQUEST    (1 << 2)
#define TPM_TIS_ACCESS_REQUEST_USE        (1 << 1)
#define TPM_TIS_ACCESS_TPM_ESTABLISHMENT  (1 << 0)

#define TPM_TIS_INT_ENABLED               (1 << 31)
#define TPM_TIS_INT_DATA_AVAILABLE        (1 << 0)
#define TPM_TIS_INT_STS_VALID             (1 << 1)
#define TPM_TIS_INT_LOCALITY_CHANGED      (1 << 2)
#define TPM_TIS_INT_COMMAND_READY         (1 << 7)

#ifndef RAISE_STS_IRQ

#define TPM_TIS_INTERRUPTS_SUPPORTED (TPM_TIS_INT_LOCALITY_CHANGED | \
                                      TPM_TIS_INT_DATA_AVAILABLE   | \
                                      TPM_TIS_INT_COMMAND_READY)

#else

#define TPM_TIS_INTERRUPTS_SUPPORTED (TPM_TIS_INT_LOCALITY_CHANGED | \
                                      TPM_TIS_INT_DATA_AVAILABLE   | \
                                      TPM_TIS_INT_STS_VALID | \
                                      TPM_TIS_INT_COMMAND_READY)

#endif

#define TPM_TIS_CAPABILITIES_SUPPORTED   ((1 << 4) | \
                                          TPM_TIS_INTERRUPTS_SUPPORTED)

#define TPM_TIS_TPM_DID       0x0001
#define TPM_TIS_TPM_VID       PCI_VENDOR_ID_IBM
#define TPM_TIS_TPM_RID       0x0001

#define TPM_TIS_NO_DATA_BYTE  0xff

/* local prototypes */

static uint64_t tpm_tis_mmio_read(void *opaque, target_phys_addr_t addr,
                                  unsigned size);

/* utility functions */

static uint8_t tpm_tis_locality_from_addr(target_phys_addr_t addr)
{
    return (uint8_t)((addr >> TPM_TIS_LOCALITY_SHIFT) & 0x7);
}

static uint32_t tpm_tis_get_size_from_buffer(const TPMSizedBuffer *sb)
{
    return be32_to_cpu(*(uint32_t *)&sb->buffer[2]);
}

static void tpm_tis_show_buffer(const TPMSizedBuffer *sb, const char *string)
{
#ifdef DEBUG_TIS
    uint32_t len, i;

    len = tpm_tis_get_size_from_buffer(sb);
    dprintf("tpm_tis: %s length = %d\n", string, len);
    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) {
            dprintf("\n");
        }
        dprintf("%.2X ", sb->buffer[i]);
    }
    dprintf("\n");
#endif
}

/*
 * Send a request to the TPM.
 */
static void tpm_tis_tpm_send(TPMState *s, uint8_t locty)
{
    TPMTISState *tis = &s->s.tis;

    tpm_tis_show_buffer(&tis->loc[locty].w_buffer, "tpm_tis: To TPM");

    s->command_locty = locty;
    s->cmd_locty = &tis->loc[locty];

    /*
     * w_offset serves as length indicator for length of data;
     * it's reset when the response comes back
     */
    tis->loc[locty].status = TPM_TIS_STATUS_EXECUTION;
    tis->loc[locty].sts &= ~TPM_TIS_STS_EXPECT;

    s->be_driver->ops->deliver_request(s->be_driver);
}

/* raise an interrupt if allowed */
static void tpm_tis_raise_irq(TPMState *s, uint8_t locty, uint32_t irqmask)
{
    TPMTISState *tis = &s->s.tis;

    if (!TPM_TIS_IS_VALID_LOCTY(locty)) {
        return;
    }

    if ((tis->loc[locty].inte & TPM_TIS_INT_ENABLED) &&
        (tis->loc[locty].inte & irqmask)) {
        dprintf("tpm_tis: Raising IRQ for flag %08x\n", irqmask);
        qemu_irq_raise(s->s.tis.irq);
        tis->loc[locty].ints |= irqmask;
    }
}

static uint32_t tpm_tis_check_request_use_except(TPMState *s, uint8_t locty)
{
    uint8_t l;

    for (l = 0; l < TPM_TIS_NUM_LOCALITIES; l++) {
        if (l == locty) {
            continue;
        }
        if ((s->s.tis.loc[l].access & TPM_TIS_ACCESS_REQUEST_USE)) {
            return 1;
        }
    }

    return 0;
}

static void tpm_tis_new_active_locality(TPMState *s, uint8_t new_active_locty)
{
    TPMTISState *tis = &s->s.tis;
    int change = (s->s.tis.active_locty != new_active_locty);

    if (change && TPM_TIS_IS_VALID_LOCTY(s->s.tis.active_locty)) {
        /* reset flags on the old active locality */
        tis->loc[s->s.tis.active_locty].access &=
                 ~(TPM_TIS_ACCESS_ACTIVE_LOCALITY|TPM_TIS_ACCESS_REQUEST_USE);
        if (TPM_TIS_IS_VALID_LOCTY(new_active_locty) &&
            tis->loc[new_active_locty].access & TPM_TIS_ACCESS_SEIZE) {
            tis->loc[tis->active_locty].access |= TPM_TIS_ACCESS_BEEN_SEIZED;
        }
    }

    tis->active_locty = new_active_locty;

    dprintf("tpm_tis: Active locality is now %d\n", s->s.tis.active_locty);

    if (TPM_TIS_IS_VALID_LOCTY(new_active_locty)) {
        /* set flags on the new active locality */
        tis->loc[new_active_locty].access |= TPM_TIS_ACCESS_ACTIVE_LOCALITY;
        tis->loc[new_active_locty].access &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                                               TPM_TIS_ACCESS_SEIZE);
    }

    if (change) {
        tpm_tis_raise_irq(s, tis->active_locty, TPM_TIS_INT_LOCALITY_CHANGED);
    }
}

/* abort -- this function switches the locality */
static void tpm_tis_abort(TPMState *s, uint8_t locty)
{
    TPMTISState *tis = &s->s.tis;

    tis->loc[locty].r_offset = 0;
    tis->loc[locty].w_offset = 0;

    dprintf("tpm_tis: tis_abort: new active locality is %d\n", tis->next_locty);

    /*
     * Need to react differently depending on who's aborting now and
     * which locality will become active afterwards.
     */
    if (tis->aborting_locty == tis->next_locty) {
        tis->loc[tis->aborting_locty].status = TPM_TIS_STATUS_READY;
        tis->loc[tis->aborting_locty].sts = TPM_TIS_STS_COMMAND_READY;
        tpm_tis_raise_irq(s, tis->aborting_locty, TPM_TIS_INT_COMMAND_READY);
    }

    /* locality after abort is another one than the current one */
    tpm_tis_new_active_locality(s, tis->next_locty);

    tis->next_locty = TPM_TIS_NO_LOCALITY;
    /* nobody's aborting a command anymore */
    tis->aborting_locty = TPM_TIS_NO_LOCALITY;
}

/* prepare aborting current command */
static void tpm_tis_prep_abort(TPMState *s, uint8_t locty, uint8_t newlocty)
{
    TPMTISState *tis = &s->s.tis;
    uint8_t busy_locty;

    tis->aborting_locty = locty;
    tis->next_locty = newlocty;  /* locality after successful abort */

    /*
     * only abort a command using an interrupt if currently executing
     * a command AND if there's a valid connection to the vTPM.
     */
    for (busy_locty = 0; busy_locty < TPM_TIS_NUM_LOCALITIES; busy_locty++) {
        if (tis->loc[busy_locty].status == TPM_TIS_STATUS_EXECUTION) {
            /*
             * there is currently no way to interrupt the TPM's operations
             * while it's executing a command; once the TPM is done and
             * returns the buffer, it will switch to the next_locty;
             */
            dprintf("tpm_tis: Locality %d is busy - deferring abort\n",
                    busy_locty);
            return;
        }
    }

    tpm_tis_abort(s, locty);
}

static void tpm_tis_receive_bh(void *opaque)
{
    TPMState *s = opaque;
    TPMTISState *tis = &s->s.tis;
    uint8_t locty = s->command_locty;

    tis->loc[locty].sts = TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE;
    tis->loc[locty].status = TPM_TIS_STATUS_COMPLETION;
    tis->loc[locty].r_offset = 0;
    tis->loc[locty].w_offset = 0;

    if (TPM_TIS_IS_VALID_LOCTY(tis->next_locty)) {
        tpm_tis_abort(s, locty);
    }

#ifndef RAISE_STS_IRQ
    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_DATA_AVAILABLE);
#else
    tpm_tis_raise_irq(s, locty,
                      TPM_TIS_INT_DATA_AVAILABLE | TPM_TIS_INT_STS_VALID);
#endif
}

/*
 * Callback from the TPM to indicate that the response was received.
 */
static void tpm_tis_receive_cb(TPMState *s, uint8_t locty)
{
    TPMTISState *tis = &s->s.tis;

    assert(s->command_locty == locty);

    qemu_bh_schedule(tis->bh);
}

/*
 * Read a byte of response data
 */
static uint32_t tpm_tis_data_read(TPMState *s, uint8_t locty)
{
    TPMTISState *tis = &s->s.tis;
    uint32_t ret = TPM_TIS_NO_DATA_BYTE;
    uint16_t len;

    if ((tis->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
        len = tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer);

        ret = tis->loc[locty].r_buffer.buffer[tis->loc[locty].r_offset++];
        if (tis->loc[locty].r_offset >= len) {
            /* got last byte */
            tis->loc[locty].sts = TPM_TIS_STS_VALID;
#ifdef RAISE_STS_IRQ
            tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
#endif
        }
        dprintf("tpm_tis: tpm_tis_data_read byte 0x%02x   [%d]\n",
                ret, tis->loc[locty].r_offset-1);
    }

    return ret;
}

#ifdef DEBUG_TIS
static void tpm_tis_dump_state(void *opaque, target_phys_addr_t addr)
{
    static const unsigned regs[] = {
        TPM_TIS_REG_ACCESS,
        TPM_TIS_REG_INT_ENABLE,
        TPM_TIS_REG_INT_VECTOR,
        TPM_TIS_REG_INT_STATUS,
        TPM_TIS_REG_INTF_CAPABILITY,
        TPM_TIS_REG_STS,
        TPM_TIS_REG_DID_VID,
        TPM_TIS_REG_RID,
        0xfff};
    int idx;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    target_phys_addr_t base = addr & ~0xfff;
    TPMState *s = opaque;
    TPMTISState *tis = &s->s.tis;

    dprintf("tpm_tis: active locality      : %d\n"
            "tpm_tis: state of locality %d : %d\n"
            "tpm_tis: register dump:\n",
            tis->active_locty,
            locty, tis->loc[locty].status);

    for (idx = 0; regs[idx] != 0xfff; idx++) {
        dprintf("tpm_tis: 0x%04x : 0x%08x\n", regs[idx],
                (uint32_t)tpm_tis_mmio_read(opaque, base + regs[idx], 4));
    }

    dprintf("tpm_tis: read offset   : %d\n"
            "tpm_tis: result buffer : ",
            tis->loc[locty].r_offset);
    for (idx = 0;
         idx < tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer);
         idx++) {
        dprintf("%c%02x%s",
                tis->loc[locty].r_offset == idx ? '>' : ' ',
                tis->loc[locty].r_buffer.buffer[idx],
                ((idx & 0xf) == 0xf) ? "\ntpm_tis:                 " : "");
    }
    dprintf("\n"
            "tpm_tis: write offset  : %d\n"
            "tpm_tis: request buffer: ",
            tis->loc[locty].w_offset);
    for (idx = 0;
         idx < tpm_tis_get_size_from_buffer(&tis->loc[locty].w_buffer);
         idx++) {
        dprintf("%c%02x%s",
                tis->loc[locty].w_offset == idx ? '>' : ' ',
                tis->loc[locty].w_buffer.buffer[idx],
                ((idx & 0xf) == 0xf) ? "\ntpm_tis:                 " : "");
    }
    dprintf("\n");
}
#endif

/*
 * Read a register of the TIS interface
 * See specs pages 33-63 for description of the registers
 */
static uint64_t tpm_tis_mmio_read(void *opaque, target_phys_addr_t addr,
                                  unsigned size)
{
    TPMState *s = opaque;
    TPMTISState *tis = &s->s.tis;
    uint16_t offset = addr & 0xffc;
    uint8_t shift = (addr & 0x3) * 8;
    uint32_t val = 0xffffffff;
    uint8_t locty = tpm_tis_locality_from_addr(addr);

    if (s->be_driver->ops->had_startup_error(s->be_driver)) {
        return val;
    }

    switch (offset) {
    case TPM_TIS_REG_ACCESS:
        /* never show the SEIZE flag even though we use it internally */
        val = tis->loc[locty].access & ~TPM_TIS_ACCESS_SEIZE;
        /* the pending flag is alawys calculated */
        if (tpm_tis_check_request_use_except(s, locty)) {
            val |= TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        val |= !s->be_driver->ops->get_tpm_established_flag(s->be_driver);
        break;
    case TPM_TIS_REG_INT_ENABLE:
        val = tis->loc[locty].inte;
        break;
    case TPM_TIS_REG_INT_VECTOR:
        val = tis->irq_num;
        break;
    case TPM_TIS_REG_INT_STATUS:
        val = tis->loc[locty].ints;
        break;
    case TPM_TIS_REG_INTF_CAPABILITY:
        val = TPM_TIS_CAPABILITIES_SUPPORTED;
        break;
    case TPM_TIS_REG_STS:
        if (tis->active_locty == locty) {
            if ((tis->loc[locty].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
                val =  (tpm_tis_get_size_from_buffer(&tis->loc[locty].r_buffer)
                        - tis->loc[locty].r_offset) << 8 | tis->loc[locty].sts;
            } else {
                val = (tis->loc[locty].w_buffer.size -
                       tis->loc[locty].w_offset) << 8 | tis->loc[locty].sts;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
        if (tis->active_locty == locty) {
            switch (tis->loc[locty].status) {
            case TPM_TIS_STATUS_COMPLETION:
                val = tpm_tis_data_read(s, locty);
                break;
            default:
                val = TPM_TIS_NO_DATA_BYTE;
                break;
            }
        }
        break;
    case TPM_TIS_REG_DID_VID:
        val = (TPM_TIS_TPM_DID << 16) | TPM_TIS_TPM_VID;
        break;
    case TPM_TIS_REG_RID:
        val = TPM_TIS_TPM_RID;
        break;
#ifdef DEBUG_TIS
    case TPM_TIS_REG_DEBUG:
        tpm_tis_dump_state(opaque, addr);
        break;
#endif
    }

    if (shift) {
        val >>= shift;
    }

    dprintf("tpm_tis:  read.%u(%08x) = %08x\n", size, (int)addr, (uint32_t)val);

    return val;
}

/*
 * Write a value to a register of the TIS interface
 * See specs pages 33-63 for description of the registers
 */
static void tpm_tis_mmio_write_intern(void *opaque, target_phys_addr_t addr,
                                      uint64_t val, unsigned size,
                                      bool hw_access)
{
    TPMState *s = opaque;
    TPMTISState *tis = &s->s.tis;
    uint16_t off = addr & 0xfff;
    uint8_t locty = tpm_tis_locality_from_addr(addr);
    uint8_t active_locty, l;
    int c, set_new_locty = 1;
    uint16_t len;

    dprintf("tpm_tis: write.%u(%08x) = %08x\n", size, (int)addr, (uint32_t)val);

    if (locty == 4 && !hw_access) {
        dprintf("tpm_tis: Access to locality 4 only allowed from hardware\n");
        return;
    }

    if (s->be_driver->ops->had_startup_error(s->be_driver)) {
        return;
    }

    switch (off) {
    case TPM_TIS_REG_ACCESS:

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            val &= ~(TPM_TIS_ACCESS_REQUEST_USE |
                     TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        }

        active_locty = tis->active_locty;

        if ((val & TPM_TIS_ACCESS_ACTIVE_LOCALITY)) {
            /* give up locality if currently owned */
            if (tis->active_locty == locty) {
                dprintf("tpm_tis: Releasing locality %d\n", locty);

                uint8_t newlocty = TPM_TIS_NO_LOCALITY;
                /* anybody wants the locality ? */
                for (c = TPM_TIS_NUM_LOCALITIES - 1; c >= 0; c--) {
                    if ((tis->loc[c].access & TPM_TIS_ACCESS_REQUEST_USE)) {
                        dprintf("tpm_tis: Locality %d requests use.\n", c);
                        newlocty = c;
                        break;
                    }
                }
                dprintf("tpm_tis: TPM_TIS_ACCESS_ACTIVE_LOCALITY: "
                        "Next active locality: %d\n",
                        newlocty);

                if (TPM_TIS_IS_VALID_LOCTY(newlocty)) {
                    set_new_locty = 0;
                    tpm_tis_prep_abort(s, locty, newlocty);
                } else {
                    active_locty = TPM_TIS_NO_LOCALITY;
                }
            } else {
                /* not currently the owner; clear a pending request */
                tis->loc[locty].access &= ~TPM_TIS_ACCESS_REQUEST_USE;
            }
        }

        if ((val & TPM_TIS_ACCESS_BEEN_SEIZED)) {
            tis->loc[locty].access &= ~TPM_TIS_ACCESS_BEEN_SEIZED;
        }

        if ((val & TPM_TIS_ACCESS_SEIZE)) {
            /*
             * allow seize if a locality is active and the requesting
             * locality is higher than the one that's active
             * OR
             * allow seize for requesting locality if no locality is
             * active
             */
            while ((TPM_TIS_IS_VALID_LOCTY(tis->active_locty) &&
                    locty > tis->active_locty) ||
                    !TPM_TIS_IS_VALID_LOCTY(tis->active_locty)) {

                /* already a pending SEIZE ? */
                if ((tis->loc[locty].access & TPM_TIS_ACCESS_SEIZE)) {
                    break;
                }

                /* check for ongoing seize by a higher locality */
                for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES; l++) {
                    if ((tis->loc[l].access & TPM_TIS_ACCESS_SEIZE)) {
                        break;
                    }
                }

                /* cancel any seize by a lower locality */
                for (l = 0; l < locty - 1; l++) {
                    tis->loc[l].access &= ~TPM_TIS_ACCESS_SEIZE;
                }

                tis->loc[locty].access |= TPM_TIS_ACCESS_SEIZE;
                dprintf("tpm_tis: TPM_TIS_ACCESS_SEIZE: "
                        "Locality %d seized from locality %d\n",
                        locty, tis->active_locty);
                dprintf("tpm_tis: TPM_TIS_ACCESS_SEIZE: Initiating abort.\n");
                set_new_locty = 0;
                tpm_tis_prep_abort(s, tis->active_locty, locty);
                break;
            }
        }

        if ((val & TPM_TIS_ACCESS_REQUEST_USE)) {
            if (tis->active_locty != locty) {
                if (TPM_TIS_IS_VALID_LOCTY(tis->active_locty)) {
                    tis->loc[locty].access |= TPM_TIS_ACCESS_REQUEST_USE;
                } else {
                    /* no locality active -> make this one active now */
                    active_locty = locty;
                }
            }
        }

        if (set_new_locty) {
            tpm_tis_new_active_locality(s, active_locty);
        }

        break;
    case TPM_TIS_REG_INT_ENABLE:
        if (tis->active_locty != locty) {
            break;
        }

        tis->loc[locty].inte = (val & (TPM_TIS_INT_ENABLED | (0x3 << 3) |
                                     TPM_TIS_INTERRUPTS_SUPPORTED));
        break;
    case TPM_TIS_REG_INT_VECTOR:
        /* hard wired -- ignore */
        break;
    case TPM_TIS_REG_INT_STATUS:
        if (tis->active_locty != locty) {
            break;
        }

        /* clearing of interrupt flags */
        if (((val & TPM_TIS_INTERRUPTS_SUPPORTED)) &&
            (tis->loc[locty].ints & TPM_TIS_INTERRUPTS_SUPPORTED)) {
            tis->loc[locty].ints &= ~val;
            if (tis->loc[locty].ints == 0) {
                qemu_irq_lower(tis->irq);
                dprintf("tpm_tis: Lowering IRQ\n");
            }
        }
        tis->loc[locty].ints &= ~(val & TPM_TIS_INTERRUPTS_SUPPORTED);
        break;
    case TPM_TIS_REG_STS:
        if (tis->active_locty != locty) {
            break;
        }

        val &= (TPM_TIS_STS_COMMAND_READY | TPM_TIS_STS_TPM_GO |
                TPM_TIS_STS_RESPONSE_RETRY);

        if (val == TPM_TIS_STS_COMMAND_READY) {
            switch (tis->loc[locty].status) {

            case TPM_TIS_STATUS_READY:
                tis->loc[locty].w_offset = 0;
                tis->loc[locty].r_offset = 0;
            break;

            case TPM_TIS_STATUS_IDLE:
                tis->loc[locty].sts = TPM_TIS_STS_COMMAND_READY;
                tis->loc[locty].status = TPM_TIS_STATUS_READY;
                tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
            break;

            case TPM_TIS_STATUS_EXECUTION:
            case TPM_TIS_STATUS_RECEPTION:
                /* abort currently running command */
                dprintf("tpm_tis: %s: Initiating abort.\n",
                        __func__);
                tpm_tis_prep_abort(s, locty, locty);
            break;

            case TPM_TIS_STATUS_COMPLETION:
                tis->loc[locty].w_offset = 0;
                tis->loc[locty].r_offset = 0;
                /* shortcut to ready state with C/R set */
                tis->loc[locty].status = TPM_TIS_STATUS_READY;
                if (!(tis->loc[locty].sts & TPM_TIS_STS_COMMAND_READY)) {
                    tis->loc[locty].sts   = TPM_TIS_STS_COMMAND_READY;
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_COMMAND_READY);
                }
            break;

            }
        } else if (val == TPM_TIS_STS_TPM_GO) {
            switch (tis->loc[locty].status) {
            case TPM_TIS_STATUS_RECEPTION:
                tpm_tis_tpm_send(s, locty);
                break;
            default:
                /* ignore */
                break;
            }
        } else if (val == TPM_TIS_STS_RESPONSE_RETRY) {
            switch (tis->loc[locty].status) {
            case TPM_TIS_STATUS_COMPLETION:
                tis->loc[locty].r_offset = 0;
                tis->loc[locty].sts = TPM_TIS_STS_VALID |
                                      TPM_TIS_STS_DATA_AVAILABLE;
                break;
            default:
                /* ignore */
                break;
            }
        }
        break;
    case TPM_TIS_REG_DATA_FIFO:
        /* data fifo */
        if (tis->active_locty != locty) {
            break;
        }

        if (tis->loc[locty].status == TPM_TIS_STATUS_IDLE ||
            tis->loc[locty].status == TPM_TIS_STATUS_EXECUTION ||
            tis->loc[locty].status == TPM_TIS_STATUS_COMPLETION) {
            /* drop the byte */
        } else {
            dprintf("tpm_tis: Byte to send to TPM: %02x\n", (uint8_t)val);
            if (tis->loc[locty].status == TPM_TIS_STATUS_READY) {
                tis->loc[locty].status = TPM_TIS_STATUS_RECEPTION;
                tis->loc[locty].sts = TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID;
            }

            if ((tis->loc[locty].sts & TPM_TIS_STS_EXPECT)) {
                if (tis->loc[locty].w_offset < tis->loc[locty].w_buffer.size) {
                    tis->loc[locty].w_buffer.
                        buffer[tis->loc[locty].w_offset++] = (uint8_t)val;
                } else {
                    tis->loc[locty].sts = TPM_TIS_STS_VALID;
                }
            }

            /* check for complete packet */
            if (tis->loc[locty].w_offset > 5 &&
                (tis->loc[locty].sts & TPM_TIS_STS_EXPECT)) {
                /* we have a packet length - see if we have all of it */
#ifdef RAISE_STS_IRQ
                bool needIrq = !(tis->loc[locty].sts & TPM_TIS_STS_VALID);
#endif
                len = tpm_tis_get_size_from_buffer(&tis->loc[locty].w_buffer);
                if (len > tis->loc[locty].w_offset) {
                    tis->loc[locty].sts = TPM_TIS_STS_EXPECT |
                                          TPM_TIS_STS_VALID;
                } else {
                    /* packet complete */
                    tis->loc[locty].sts = TPM_TIS_STS_VALID;
                }
#ifdef RAISE_STS_IRQ
                if (needIrq) {
                    tpm_tis_raise_irq(s, locty, TPM_TIS_INT_STS_VALID);
                }
#endif
            }
        }
        break;
    }
}

static void tpm_tis_mmio_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned size)
{
    return tpm_tis_mmio_write_intern(opaque, addr, val, size, false);
}

static const MemoryRegionOps tpm_tis_memory_ops = {
    .read = tpm_tis_mmio_read,
    .write = tpm_tis_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static int tpm_tis_do_startup_tpm(TPMState *s)
{
    return s->be_driver->ops->startup_tpm(s->be_driver);
}

/*
 * This function is called when the machine starts, resets or due to
 * S3 resume.
 */
static void tpm_tis_reset(DeviceState *d)
{
    TPMState *s = DO_UPCAST(TPMState, busdev.qdev, d);
    TPMTISState *tis = &s->s.tis;
    int c;

    s->be_driver->ops->reset(s->be_driver);

    tis->active_locty = TPM_TIS_NO_LOCALITY;
    tis->next_locty = TPM_TIS_NO_LOCALITY;
    tis->aborting_locty = TPM_TIS_NO_LOCALITY;

    for (c = 0; c < TPM_TIS_NUM_LOCALITIES; c++) {
        tis->loc[c].access = TPM_TIS_ACCESS_TPM_REG_VALID_STS;
        tis->loc[c].sts = 0;
        tis->loc[c].inte = (1 << 3);
        tis->loc[c].ints = 0;
        tis->loc[c].status = TPM_TIS_STATUS_IDLE;

        tis->loc[c].w_offset = 0;
        s->be_driver->ops->realloc_buffer(&tis->loc[c].w_buffer);
        tis->loc[c].r_offset = 0;
        s->be_driver->ops->realloc_buffer(&tis->loc[c].r_buffer);
    }

    tpm_tis_do_startup_tpm(s);
}

static int tpm_tis_init(ISADevice *dev)
{
    TPMState *s = DO_UPCAST(TPMState, busdev, dev);
    TPMTISState *tis = &s->s.tis;
    int rc;

    s->be_driver = qemu_find_tpm(s->backend);
    if (!s->be_driver) {
        error_report("tpm_tis: backend driver with id %s could not be "
                     "found.n\n", s->backend);
        goto err_exit;
    }

    s->be_driver->fe_model = "tpm-tis";

    if (s->be_driver->ops->init(s->be_driver, s, tpm_tis_receive_cb)) {
        goto err_exit;
    }

    tis->bh = qemu_bh_new(tpm_tis_receive_bh, s);

    if (tis->irq_num > 15) {
        error_report("IRQ %d for TPM TIS is outside valid range of 0 to 15.\n",
                     tis->irq_num);
        goto err_exit;
    }

    isa_init_irq(dev, &tis->irq, tis->irq_num);

    memory_region_init_io(&s->mmio, &tpm_tis_memory_ops, s, "tpm-tis-mmio",
                          TPM_TIS_NUM_LOCALITIES << TPM_TIS_LOCALITY_SHIFT);
    memory_region_add_subregion(get_system_memory(), TPM_TIS_ADDR_BASE,
                                &s->mmio);

    rc = tpm_tis_do_startup_tpm(s);
    if (rc != 0) {
        goto err_destroy_memory;
    }

    return 0;

 err_destroy_memory:
    memory_region_del_subregion(get_system_memory(), &s->mmio);
    memory_region_destroy(&s->mmio);

 err_exit:
    return -1;
}

static const VMStateDescription vmstate_tpm_tis = {
    .name = "tpm",
    .unmigratable = 1,
};

static Property tpm_tis_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMState,
                       s.tis.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_STRING("tpmdev", TPMState, backend),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);

    ic->init = tpm_tis_init;

    dc->props = tpm_tis_properties;
    dc->reset = tpm_tis_reset;
    dc->vmsd  = &vmstate_tpm_tis;
}

static TypeInfo tpm_tis_info = {
    .name        = "tpm-tis",
    .parent      = TYPE_ISA_DEVICE,
    .class_init  = tpm_tis_class_initfn,
    .instance_size = sizeof(TPMState),
};

static void tpm_tis_register(void)
{
    type_register_static(&tpm_tis_info);
}

type_init(tpm_tis_register)
