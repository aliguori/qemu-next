/*
 * Human Monitor Interface
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hmp.h"
#include "qmp-commands.h"

void hmp_info_name(Monitor *mon)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->has_name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qapi_free_NameInfo(info);
}

void hmp_eject(Monitor *mon, const QDict *qdict)
{
    int force = qdict_get_try_bool(qdict, "force", 0);
    const char *device = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(device, true, force, &err);
    if (err) {
        monitor_printf(mon, "eject: %s\n", error_get_pretty(err));
        error_free(err);
    }
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_set_blockdev_password(device, password, &err);
    if (err) {
        monitor_printf(mon, "block_passwd: %s\n", error_get_pretty(err));
        error_free(err);
    }
}
