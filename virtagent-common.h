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
#include "qemu-common.h"
#include "qemu_socket.h"
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

#define VERSION "1.0"
#define EOL "\r\n"

#define VA_HDR_LEN_MAX 4096 /* http header limit */
#define VA_CONTENT_LEN_MAX 2*1024*1024 /* rpc/http send limit */
#define VA_CLIENT_JOBS_MAX 5 /* max client rpcs we can queue */
#define VA_SERVER_JOBS_MAX 1 /* max server rpcs we can queue */

enum va_ctx {
    VA_CTX_HOST,
    VA_CTX_GUEST,
};

enum va_job_status {
    VA_JOB_STATUS_PENDING = 0,
    VA_JOB_STATUS_OK,
    VA_JOB_STATUS_ERROR,
    VA_JOB_STATUS_CANCELLED,
};

int va_init(enum va_ctx ctx, int fd);
int va_client_job_add(xmlrpc_mem_block *req_xml, VAClientCallback *cb,
                      MonitorCompletion *mon_cb, void *mon_data);
#endif /* VIRTAGENT_COMMON_H */