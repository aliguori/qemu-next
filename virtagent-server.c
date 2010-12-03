/*
 * virtagent - host/guest RPC server functions
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
#include <syslog.h>
#include "qemu_socket.h"
#include "virtagent-common.h"

static bool va_enable_syslog = false; /* enable syslog'ing of RPCs */

#define SLOG(msg, ...) do { \
    char msg_buf[1024]; \
    if (!va_enable_syslog) { \
        break; \
    } \
    snprintf(msg_buf, 1024, msg, ## __VA_ARGS__); \
    syslog(LOG_INFO, "virtagent, %s", msg_buf); \
} while(0)

typedef struct RPCFunction {
    xmlrpc_value *(*func)(xmlrpc_env *env, xmlrpc_value *param, void *unused);
    const char *func_name;
} RPCFunction;

static RPCFunction guest_functions[] = {
    { NULL, NULL }
};
static RPCFunction host_functions[] = {
    { NULL, NULL }
};

static void va_register_functions(xmlrpc_env *env, xmlrpc_registry *registry,
                                  RPCFunction *list)
{
    int i;
    for (i = 0; list[i].func != NULL; ++i) {
        TRACE("adding func: %s", list[i].func_name);
        xmlrpc_registry_add_method(env, registry, NULL, list[i].func_name,
                                   list[i].func, NULL);
    }
}

int va_server_init(VAServerData *server_data, bool is_host)
{
    RPCFunction *func_list = is_host ? host_functions : guest_functions;

    va_enable_syslog = !is_host; /* enable logging for guest agent */
    xmlrpc_env_init(&server_data->env);
    server_data->registry = xmlrpc_registry_new(&server_data->env);
    va_register_functions(&server_data->env, server_data->registry, func_list);

    return 0;
}
