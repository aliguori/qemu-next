#include "hmp.h"

/*******************************************************/
/*                        HMP                          */
/*******************************************************/

/* These should not access any QEMU internals.  Just use QMP interfaces. */

int hmp_quit(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    monitor_suspend(mon);
    qmp_quit(NULL);
    return 0;
}

int hmp_eject(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    int force = qdict_get_try_bool(qdict, "force", 0);
    const char *filename = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(filename, true, force, &err);
    if (err) {
        qerror_report_err(err);
        return -1;
    }

    return 0;
}

int hmp_block_passwd(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_set_blockdev_password(device, password, &err);
    if (err) {
        monitor_printf(mon, "block_passwd: %s\n", error_get_pretty(err));
        error_free(err);
        return -1;
    }

    return 0;
}

static void cb_hmp_change_bdrv_pwd(Monitor *mon, const char *password,
                                   void *opaque)
{
    Error *encryption_err = opaque;
    Error *err = NULL;

    qmp_block_passwd(error_get_field(encryption_err, "device"),
                     password, &err);
    if (err) {
        monitor_printf(mon, "invalid password\n");
        error_free(err);
    }

    error_free(encryption_err);

    monitor_read_command(mon, 1);
}

int hmp_change(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    Error *err = NULL;

    qmp_change(device, target, !!arg, arg, &err);
    if (error_is_type(err, QERR_DEVICE_ENCRYPTED)) {
        monitor_printf(mon, "%s (%s) is encrypted.\n",
                       error_get_field(err, "device"),
                       error_get_field(err, "encrypted_filename"));
        if (!monitor_get_rs(mon)) {
            monitor_printf(mon,
                           "terminal does not support password prompting\n");
            error_free(err);
            return -1;
        }
        readline_start(monitor_get_rs(mon), "Password: ", 1,
                       cb_hmp_change_bdrv_pwd, err);
    }

    return 0;
}

int hmp_screendump(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    qmp_screendump(qdict_get_str(qdict, "filename"), NULL);
    return 0;
}

int hmp_stop(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    qmp_stop(NULL);
    return 0;
}

int hmp_cont(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    qmp_cont(NULL);
    return 0;
}

void hmp_info_version(Monitor *mon)
{
    VersionInfo *info;

    info = qmp_query_version(NULL);

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                   info->qemu.major, info->qemu.minor, info->qemu.micro,
                   info->package);

    qmp_free_version_info(info);
}

void hmp_info_status(Monitor *mon)
{
    StatusInfo *info;

    info = qmp_query_status(NULL);

    monitor_printf(mon, "VM status: %s%s\n",
                   info->running ? "running" : "paused",
                   info->singlestep ? " (single step mode)" : "");

    qmp_free_status_info(info);
}

void hmp_info_block(Monitor *mon)
{
    BlockInfo *block_list, *info;

    block_list = qmp_query_block(NULL);

    for (info = block_list; info; info = info->next) {
        monitor_printf(mon, "%s: type=%s removable=%d",
                       info->device, info->type, info->removable);

        if (info->removable) {
            monitor_printf(mon, " locked=%d", info->locked);
        }

        if (info->has_inserted) {
            monitor_printf(mon, " file=");
            monitor_print_filename(mon, info->inserted->file);

            if (info->inserted->has_backing_file) {
                monitor_printf(mon, " backing_file=");
                monitor_print_filename(mon, info->inserted->backing_file);
            }
            monitor_printf(mon, " ro=%d drv=%s encrypted=%d",
                           info->inserted->ro, info->inserted->drv,
                           info->inserted->encrypted);
        } else {
            monitor_printf(mon, " [not inserted]");
        }

        monitor_printf(mon, "\n");
    }

    qmp_free_block_info(block_list);
}

void hmp_info_blockstats(Monitor *mon)
{
    BlockStats *stats_list, *stats;

    stats_list = qmp_query_blockstats(NULL);

    for (stats = stats_list; stats; stats = stats->next) {
        if (!stats->has_device) {
            continue;
        }

        monitor_printf(mon, "%s:", stats->device);
        monitor_printf(mon, " rd_bytes=%" PRId64
                       " wr_bytes=%" PRId64
                       " rd_operations=%" PRId64
                       " wr_operations=%" PRId64
                       "\n",
                       stats->stats->rd_bytes,
                       stats->stats->wr_bytes,
                       stats->stats->rd_operations,
                       stats->stats->wr_operations);
    }

    qmp_free_block_stats(stats_list);
}

void hmp_info_vnc(Monitor *mon)
{
    VncInfo *info;
    VncClientInfo *client;

    info = qmp_query_vnc(NULL);

    if (!info->enabled) {
        monitor_printf(mon, "Server: disabled\n");
        return;
    }

    monitor_printf(mon, "Server:\n");
    if (info->has_host && info->has_service) {
        monitor_printf(mon, "     address: %s:%s\n", info->host, info->service);
    }
    if (info->has_auth) {
        monitor_printf(mon, "        auth: %s\n", info->auth);
    }

    if (!info->has_clients || info->clients == NULL) {
        monitor_printf(mon, "Client: none\n");
    } else {
        for (client = info->clients; client; client = client->next) {
            monitor_printf(mon, "Client:\n");
            monitor_printf(mon, "     address: %s:%s\n",
                           client->host, client->service);
            if (client->has_x509_dname) {
                monitor_printf(mon, "  x509_dname: %s\n", client->x509_dname);
            } else {
                monitor_printf(mon, "  x509_dname: none\n");
            }
            monitor_printf(mon, "    username: %s\n",
                           client->has_sasl_username ?
                           client->sasl_username : "none");
        }
    }

    qmp_free_vnc_info(info);
}

void hmp_info_name(Monitor *mon)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->has_name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qmp_free_name_info(info);
}

void hmp_info_uuid(Monitor *mon)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qmp_free_uuid_info(info);
}
