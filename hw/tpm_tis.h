/*
 * tpm_tis.c - QEMU's TPM TIS interface emulator
 *
 * Copyright (C) 2006,2010,2011 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputiggroup.org
 *
 */
#ifndef HW_TPM_TIS_H
#define HW_TPM_TIS_H

#include "isa.h"
#include "qemu-common.h"

#define TPM_TIS_ADDR_BASE           0xFED40000

#define TPM_TIS_NUM_LOCALITIES      5     /* per spec */
#define TPM_TIS_LOCALITY_SHIFT      12
#define TPM_TIS_NO_LOCALITY         0xff

#define TPM_TIS_IS_VALID_LOCTY(x)   ((x) < TPM_TIS_NUM_LOCALITIES)

#define TPM_TIS_IRQ                 5

#define TPM_TIS_BUFFER_MAX          4096


typedef struct TPMSizedBuffer {
    uint32_t size;
    uint8_t  *buffer;
} TPMSizedBuffer;

typedef enum {
    TPM_TIS_STATUS_IDLE = 0,
    TPM_TIS_STATUS_READY,
    TPM_TIS_STATUS_COMPLETION,
    TPM_TIS_STATUS_EXECUTION,
    TPM_TIS_STATUS_RECEPTION,
} TPMTISStatus;

/* locality data  -- all fields are persisted */
typedef struct TPMLocality {
    TPMTISStatus status;
    uint8_t access;
    uint8_t sts;
    uint32_t inte;
    uint32_t ints;

    uint16_t w_offset;
    uint16_t r_offset;
    TPMSizedBuffer w_buffer;
    TPMSizedBuffer r_buffer;
} TPMLocality;

typedef struct TPMTISState {
    QEMUBH *bh;
    uint32_t offset;
    uint8_t buf[TPM_TIS_BUFFER_MAX];

    uint8_t active_locty;
    uint8_t aborting_locty;
    uint8_t next_locty;

    TPMLocality loc[TPM_TIS_NUM_LOCALITIES];

    qemu_irq irq;
    uint32_t irq_num;
} TPMTISState;

#endif /* HW_TPM_TIS_H */
