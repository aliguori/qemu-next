/*
 * QEMU Management Protocol
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

#include "qemu-common.h"
#include "qmp-commands.h"

#include "sysemu.h"
#include "console.h"

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = g_malloc0(sizeof(*info));

    if (qemu_name) {
        info->has_name = true;
        info->name = g_strdup(qemu_name);
    }

    return info;
}

void qmp_change_vnc_password(const char *password, Error **err)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_set(err, QERR_SET_PASSWD_FAILED);
    }
}
