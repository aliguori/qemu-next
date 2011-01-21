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
#ifndef VIRTAGENT_COMMON_H
#define VIRTAGENT_COMMON_H

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>
#include <xmlrpc-c/server.h>
#include <termios.h>
#include "qemu-common.h"
#include "qemu_socket.h"
#include "qemu-timer.h"
#include "monitor.h"
#include "virtagent-server.h"
#include "virtagent.h"

#define DEBUG_VA

#ifdef DEBUG_VA
#define TRACE(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)
#else
#define TRACE(msg, ...) \
    do { } while (0)
#endif

#define LOG(msg, ...) do { \
    fprintf(stderr, "%s:%s(): " msg "\n", \
            __FILE__, __FUNCTION__, ## __VA_ARGS__); \
} while(0)

#define VA_VERSION "1.0"
#define EOL "\r\n"

#define VA_PIDFILE "/var/run/qemu-va.pid"
#define VA_HDR_LEN_MAX 4096 /* http header limit */
#define VA_CONTENT_LEN_MAX 2*1024*1024 /* rpc/http send limit */
#define VA_CLIENT_JOBS_MAX 5 /* max client rpcs we can queue */
#define VA_SERVER_JOBS_MAX 5 /* max server rpcs we can queue */
#define VA_SERVER_TIMEOUT_MS 5 * 1000
#define VA_CLIENT_TIMEOUT_MS 5 * 1000
#define VA_SENTINEL 0xFF
#define VA_BAUDRATE B38400 /* for isa-serial channels */

typedef struct VAContext {
    bool is_host;
    const char *channel_method;
    const char *channel_path;
} VAContext;

enum va_job_status {
    VA_JOB_STATUS_PENDING = 0,
    VA_JOB_STATUS_OK,
    VA_JOB_STATUS_ERROR,
    VA_JOB_STATUS_CANCELLED,
};

int va_init(VAContext ctx);
int va_client_job_add(xmlrpc_mem_block *req_xml, VAClientCallback *cb,
                      MonitorCompletion *mon_cb, void *mon_data);
int va_server_job_add(xmlrpc_mem_block *resp_xml, const char client_tag[64]);
#endif /* VIRTAGENT_COMMON_H */
