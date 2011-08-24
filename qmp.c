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
#include "blockdev.h"

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

void qmp_change_vnc_listen(const char *target, Error **err)
{
    if (vnc_display_open(NULL, target) < 0) {
        error_set(err, QERR_VNC_SERVER_FAILED, target);
    }
}

void qmp_change(const char *device, const char *target,
                bool has_arg, const char *arg, Error **err)
{
    if (strcmp(device, "vnc") == 0) {
        if (strcmp(target, "passwd") == 0 || strcmp(target, "password") == 0) {
            if (!has_arg || !arg[0]) {
                vnc_display_disable_login(NULL);
            } else {
                qmp_change_vnc_password(arg, err);
            }
        } else {
            qmp_change_vnc_listen(target, err);
        }
    } else {
        deprecated_qmp_change_blockdev(device, target, has_arg, arg, err);
    }
}
