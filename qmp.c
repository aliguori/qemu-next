/*
 * QAPI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#include "qemu-common.h"
#include "qmp-core.h"
#include "qmp.h"
#include "sysemu.h"

VersionInfo *qmp_query_version(Error **err)
{
    VersionInfo *info = qmp_alloc_version_info();
    const char *version = QEMU_VERSION;
    char *tmp;

    info->qemu.major = strtol(version, &tmp, 10);
    tmp++;
    info->qemu.minor = strtol(tmp, &tmp, 10);
    tmp++;
    info->qemu.micro = strtol(tmp, &tmp, 10);
    info->package = qemu_strdup(QEMU_PKGVERSION);

    return info;
}

void qmp_quit(Error **err)
{
    no_shutdown = 0;
    qemu_system_shutdown_request();
}
