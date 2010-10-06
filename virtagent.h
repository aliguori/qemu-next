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
#include "virtagent-common.h"

#define GUEST_AGENT_PATH_CLIENT "/tmp/virtagent-guest-client.sock"
#define HOST_AGENT_PATH_CLIENT "/tmp/virtagent-host-client.sock"
#define VA_MAX_CHUNK_SIZE 4096 /* max bytes at a time for get/send file */

int do_agent_viewfile(Monitor *mon, const QDict *params, QObject **ret_data);
int do_agent_copyfile(Monitor *mon, const QDict *params, QObject **ret_data);

#endif /* VIRTAGENT_H */
