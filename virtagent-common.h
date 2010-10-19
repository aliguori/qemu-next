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

typedef void (RPCRequestCallback)(void *rpc_data);
typedef struct RPCRequest {
    RPCRequestCallback *cb;
    xmlrpc_mem_block *req_xml;
    char *resp_xml;
    int resp_xml_len;
    Monitor *mon;
    MonitorCompletion *mon_cb;
    void *mon_data;
} RPCRequest;

int va_send_rpc_response(int fd, const xmlrpc_mem_block *resp_xml);
int va_get_rpc_request(int fd, char **req_xml, int *req_len);
int va_transport_rpc_call(int fd, RPCRequest *rpc_data);
#endif /* VIRTAGENT_COMMON_H */
