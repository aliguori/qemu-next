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
 *
 * Based on net.c
 */
#include "config.h"

#include "monitor.h"
#include "qerror.h"
#include "tpm.h"
#include "qmp-commands.h"

static QLIST_HEAD(, TPMBackend) tpm_backends =
    QLIST_HEAD_INITIALIZER(tpm_backends);

#ifdef CONFIG_TPM

static const TPMDriverOps *bes[] = {
    NULL,
};

const TPMDriverOps *tpm_get_backend_driver(const char *id)
{
    int i;

    for (i = 0; bes[i] != NULL; i++) {
        if (!strcmp(bes[i]->id, id)) {
            break;
        }
    }

    return bes[i];
}

/*
 * Walk the list of available TPM backend drivers and display them on the
 * screen.
 */
void tpm_display_backend_drivers(void)
{
    int i;

    fprintf(stderr, "Supported TPM types (choose only one):\n");

    for (i = 0; bes[i] != NULL; i++) {
        fprintf(stderr, "%12s   %s\n", bes[i]->id, bes[i]->desc());
    }
    fprintf(stderr, "\n");
}

/*
 * Find the TPM with the given Id
 */
TPMBackend *qemu_find_tpm(const char *id)
{
    TPMBackend *drv;

    QLIST_FOREACH(drv, &tpm_backends, list) {
        if (!strcmp(drv->id, id)) {
            return drv;
        }
    }

    return NULL;
}

static int configure_tpm(QemuOpts *opts)
{
    const char *value;
    const char *id;
    const TPMDriverOps *be;
    TPMBackend *drv;

    if (!QLIST_EMPTY(&tpm_backends)) {
        error_report("Only one TPM is allowed.\n");
        return 1;
    }

    id = qemu_opts_id(opts);
    if (id == NULL) {
        qerror_report(QERR_MISSING_PARAMETER, "id");
        return 1;
    }

    value = qemu_opt_get(opts, "type");
    if (!value) {
        qerror_report(QERR_MISSING_PARAMETER, "type");
        tpm_display_backend_drivers();
        return 1;
    }

    be = tpm_get_backend_driver(value);
    if (be == NULL) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "type",
                      "a TPM backend type");
        tpm_display_backend_drivers();
        return 1;
    }

    drv = be->create(opts, id);
    if (!drv) {
        return 1;
    }

    QLIST_INSERT_HEAD(&tpm_backends, drv, list);

    return 0;
}

static int tpm_init_tpmdev(QemuOpts *opts, void *dummy)
{
    return configure_tpm(opts);
}

/*
 * Walk the list of TPM backend drivers that are in use and call their
 * destroy function to have them cleaned up.
 */
void tpm_cleanup(void)
{
    TPMBackend *drv, *next;

    QLIST_FOREACH_SAFE(drv, &tpm_backends, list, next) {
        QLIST_REMOVE(drv, list);
        drv->ops->destroy(drv);
    }
}

/*
 * Initialize the TPM. Process the tpmdev command line options describing the
 * TPM backend.
 */
int tpm_init(void)
{
    if (qemu_opts_foreach(qemu_find_opts("tpmdev"),
                          tpm_init_tpmdev, NULL, 1) != 0) {
        return -1;
    }

    atexit(tpm_cleanup);

    return 0;
}

/*
 * Parse the TPM configuration options.
 * It is possible to pass an option '-tpmdev none' to not activate any TPM.
 * To display all available TPM backends the user may use '-tpmdev ?'
 */
int tpm_config_parse(QemuOptsList *opts_list, const char *optarg)
{
    QemuOpts *opts;

    if (strcmp("none", optarg) != 0) {
        if (*optarg == '?') {
            tpm_display_backend_drivers();
            return -1;
        }
        opts = qemu_opts_parse(opts_list, optarg, 1);
        if (!opts) {
            return -1;
        }
    }
    return 0;
}

#endif /* CONFIG_TPM */

static TPMInfo *qmp_query_tpm_inst(TPMBackend *drv)
{
    TPMInfo *res = g_new0(TPMInfo, 1);

    res->model = g_strdup(drv->fe_model);
    res->id = g_strdup(drv->id);
    if (drv->path) {
        res->path = g_strdup(drv->path);
    }
    res->type = g_strdup(drv->ops->id);

    return res;
}

/*
 * Walk the list of active TPM backends and collect information about them
 * following the schema description in qapi-schema.json.
 */
TPMInfoList *qmp_query_tpm(Error **errp)
{
    TPMBackend *drv;
    TPMInfoList *info, *head = NULL, *cur_item = NULL;

    QLIST_FOREACH(drv, &tpm_backends, list) {
        info = g_new0(TPMInfoList, 1);
        info->value = qmp_query_tpm_inst(drv);

        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}
