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
#include "virtagent-manager.h"

#define VA_GUEST_PATH_VIRTIO_DEFAULT "/dev/virtio-ports/org.qemu.virtagent"
#define VA_HOST_PATH_DEFAULT "/tmp/virtagent.sock"
#define VA_MAX_CHUNK_SIZE 4096 /* max bytes at a time for get/send file */

typedef void (VAClientCallback)(QDict *resp,
                                MonitorCompletion *mon_cb, void *mon_data);
typedef struct VAClientData {
    QList *supported_methods;
    char guest_version[32];
    bool enabled;
    VAManager *manager;
} VAClientData;

int va_client_init(VAManager *m, VAClientData *client_data);
int va_client_close(void);
void va_client_read_response_done(const char *content, size_t content_len,
                                  const char *tag);
void do_agent_viewfile_print(Monitor *mon, const QObject *qobject);
int do_agent_viewfile(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);
void do_agent_viewdmesg_print(Monitor *mon, const QObject *qobject);
int do_agent_viewdmesg(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);
int do_agent_shutdown(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque);
void do_va_capabilities_print(Monitor *mon, const QObject *qobject);
int do_va_capabilities(Monitor *mon, const QDict *mon_params,
                       MonitorCompletion cb, void *opaque);
void do_va_ping_print(Monitor *mon, const QObject *qobject);
int do_va_ping(Monitor *mon, const QDict *mon_params,
               MonitorCompletion cb, void *opaque);
int do_va_shutdown(Monitor *mon, const QDict *params,
                   MonitorCompletion cb, void *opaque);
int va_client_init_capabilities(void);
int va_send_hello(void);
/*
int do_va_cmd(Monitor *mon, const QDict *params,
              MonitorCompletion cb, void *opaque);
              */

#endif /* VIRTAGENT_H */