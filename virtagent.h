/*
 * virt-agent - host/guest RPC client functions
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Adam Litke        <aglitke@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VIRTAGENT_H
#define VIRTAGENT_H

#include "monitor.h"

#define GUEST_AGENT_PATH_CLIENT "/tmp/virtagent-guest-client.sock"
#define HOST_AGENT_PATH_CLIENT "/tmp/virtagent-host-client.sock"
#define VA_MAX_CHUNK_SIZE 4096 /* max bytes at a time for get/send file */

typedef void (VAClientCallback)(const char *resp_data, size_t resp_data_len,
                                MonitorCompletion *mon_cb, void *mon_data);
typedef struct VAClientData {
    QList *supported_methods;
} VAClientData;

int va_client_init(VAClientData *client_data);
void do_agent_viewfile_print(Monitor *mon, const QObject *qobject);
int do_agent_viewfile(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);
void do_agent_viewdmesg_print(Monitor *mon, const QObject *qobject);
int do_agent_viewdmesg(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);
int do_agent_shutdown(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);

#endif /* VIRTAGENT_H */
