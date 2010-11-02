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

#include <stdlib.h>
#include <stdio.h>
#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>
#include <xmlrpc-c/server.h>
#include "qemu-common.h"
#include "qemu_socket.h"
#include "monitor.h"
#include "virtproxy.h"

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

#define TADDR "127.0.0.1:8080"
#define URL "http://localhost:8080/RPC2"
#define NAME "QEMU virt-agent host client"
#define VERSION "1.0"
#define EOL "\r\n"

#define VA_RPC_STATUS_OK 0
#define VA_RPC_STATUS_ERR 1
#define VA_HDR_LEN_MAX 4096 /* http header limit */
#define VA_CONTENT_LEN_MAX 2*1024*1024 /* rpc/http send limit */

typedef void (VARPCDataCallback)(void *rpc_data);
typedef struct VARPCData {
    VARPCDataCallback *cb;
    int status;
    void *opaque;
    /* provided/allocated by caller for sending as memblocks */
    xmlrpc_mem_block *send_req_xml;
    xmlrpc_mem_block *send_resp_xml;
    /* recieved, and passed to cb func, as char arrays */
    char *req_xml;
    int req_xml_len;
    char *resp_xml;
    int resp_xml_len;
    /* for use by QMP functions */
    MonitorCompletion *mon_cb;
    void *mon_data;
} VARPCData;

int va_rpc_send_request(VARPCData *data, int fd);
int va_rpc_read_request(VARPCData *data, int fd);
#endif /* VIRTAGENT_COMMON_H */
