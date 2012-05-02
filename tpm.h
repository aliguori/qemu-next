/*
 * TPM configuration
 *
 * Copyright (C) 2011 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_TPM_H
#define QEMU_TPM_H

#include "memory.h"
#include "hw/tpm_tis.h"

struct TPMDriverOps;
typedef struct TPMDriverOps TPMDriverOps;

typedef struct TPMBackend {
    char *id;
    const char *fe_model;
    char *path;
    const TPMDriverOps *ops;

    QLIST_ENTRY(TPMBackend) list;
} TPMBackend;

/* overall state of the TPM interface */
typedef struct TPMState {
    ISADevice busdev;
    MemoryRegion mmio;

    union {
        TPMTISState tis;
    } s;

    uint8_t     command_locty;
    TPMLocality *cmd_locty;

    char *backend;
    TPMBackend *be_driver;
} TPMState;

typedef void (TPMRecvDataCB)(TPMState *, uint8_t locty);

struct TPMDriverOps {
    const char *id;
    /* get a descriptive text of the backend to display to the user */
    const char *(*desc)(void);

    TPMBackend *(*create)(QemuOpts *opts, const char *id);
    void (*destroy)(TPMBackend *t);

    /* initialize the backend */
    int (*init)(TPMBackend *t, TPMState *s, TPMRecvDataCB *datacb);
    /* start up the TPM on the backend */
    int (*startup_tpm)(TPMBackend *t);
    /* returns true if nothing will ever answer TPM requests */
    bool (*had_startup_error)(TPMBackend *t);

    size_t (*realloc_buffer)(TPMSizedBuffer *sb);

    void (*deliver_request)(TPMBackend *t);

    void (*reset)(TPMBackend *t);

    bool (*get_tpm_established_flag)(TPMBackend *t);
};

#define TPM_DEFAULT_DEVICE_MODEL "tpm-tis"

int tpm_config_parse(QemuOptsList *opts_list, const char *optarg);
int tpm_init(void);
void tpm_cleanup(void);
TPMBackend *qemu_find_tpm(const char *id);
void tpm_display_backend_drivers(void);
const TPMDriverOps *tpm_get_backend_driver(const char *id);

#endif /* QEMU_TPM_H */
