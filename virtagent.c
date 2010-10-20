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

#include "sysemu.h"
#include "monitor.h"
#include "qjson.h"
#include "qint.h"
#include "cpu-common.h"
#include "kvm.h"
#include "trace.h"
#include "qemu_socket.h"
#include "xmlrpc.h"
#include "xmlrpc_client.h"
#include "virtagent-daemon.h"
#include "virtagent-common.h"
#include "virtagent.h"

static int rpc_has_error(xmlrpc_env *env)
{
    if (env->fault_occurred) {
        LOG("An RPC error has occurred (%i): %s\n", env->fault_code, env->fault_string);
        //qerror_report(QERR_RPC_FAILED, env->fault_code, env->fault_string);
        return -1;
    }
    return 0;
}

/*
 * Get a connected socket that can be used to make an RPC call
 * This interface will eventually return the connected virtproxy socket for the
 * virt-agent channel
 */
static int get_transport_fd(void)
{
    /* TODO: eventually this will need a path that is unique to other
     * instances of qemu-vp/qemu. for the integrated qemu-vp we should
     * explore the possiblity of not requiring a unix socket under the
     * covers, as well as having client init code set up the oforward
     * for the service rather than qemu-vp
     */
    int ret;
    int fd = unix_connect(GUEST_AGENT_PATH_CLIENT);
    if (fd < 0) {
        LOG("failed to connect to virtagent service");
    }
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    return fd;
}

static int rpc_execute(xmlrpc_env *const env, const char *function,
                       xmlrpc_value *params, RPCRequest *rpc_data)
{
    xmlrpc_mem_block *call_xml;
    int fd, ret;

    fd = get_transport_fd();
    if (fd < 0) {
        LOG("invalid fd");
        ret = -1;
        goto out;
    }

    call_xml = XMLRPC_MEMBLOCK_NEW(char, env, 0);
    xmlrpc_serialize_call(env, call_xml, function, params);
    if (rpc_has_error(env)) {
        ret = -EREMOTE;
        goto out_callxml;
    }

    rpc_data->req_xml = call_xml;
    ret = va_transport_rpc_call(fd, rpc_data);
    if (ret != 0) {
        ret = -1;
        goto out_callxml;
    } else {
        ret = 0;
        goto out;
    }

out_callxml:
    XMLRPC_MEMBLOCK_FREE(char, call_xml);
out:
    return ret;
}

static void do_agent_viewfile_cb(void *opaque)
{
    RPCRequest *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    char *file_contents = NULL;
    char format[32];
    int file_size, ret, i;
    xmlrpc_env env;

    if (rpc_data->resp_xml == NULL) {
        monitor_printf(rpc_data->mon, "error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out_no_resp;
    }

    xmlrpc_parse_value(&env, resp, "6", &file_contents, &file_size);
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out;
    }

    if (file_contents != NULL) {
         /* monitor_printf truncates so do it in chunks. also, file_contents
          * may not be null-termed at proper location so explicitly calc
          * last chunk sizes */
        for (i = 0; i < file_size - 1024; i += 1024) {
            monitor_printf(rpc_data->mon, "%.1024s", file_contents + i);
        }
        sprintf(format, "%%.%ds\n", file_size - i);
        monitor_printf(rpc_data->mon, format, file_contents + i);
    }

out:
    qemu_free(rpc_data->resp_xml);
    xmlrpc_DECREF(resp);
out_no_resp:
    rpc_data->mon_cb(rpc_data->mon_data, NULL);
    qemu_free(rpc_data);
}

/*
 * do_agent_viewfile(): View a text file in the guest
 */
int do_agent_viewfile(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    RPCRequest *rpc_data;
    const char *filepath;
    int ret;

    filepath = qdict_get_str(mon_params, "filepath");
    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "(s)", filepath);
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(RPCRequest));
    rpc_data->cb = do_agent_viewfile_cb;
    rpc_data->mon = mon;
    rpc_data->mon_cb = cb;
    rpc_data->mon_data = opaque;

    ret = rpc_execute(&env, "getfile", params, rpc_data);
    if (ret == -EREMOTE) {
        monitor_printf(mon, "RPC Failed (%i): %s\n", env.fault_code,
                       env.fault_string);
        return -1;
    } else if (ret == -1) {
        monitor_printf(mon, "RPC communication error\n");
        return -1;
    }

    return 0;
}

static void do_agent_viewdmesg_cb(void *opaque)
{
    RPCRequest *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    char *dmesg = NULL;
    int ret, i;
    xmlrpc_env env;

    if (rpc_data->resp_xml == NULL) {
        monitor_printf(rpc_data->mon, "error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    TRACE("resp_xml:\n%s", rpc_data->resp_xml);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out_no_resp;
    }

    xmlrpc_parse_value(&env, resp, "s", &dmesg);
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out;
    }

    if (dmesg != NULL) {
        /* monitor_printf truncates */
        for (i=0; i < strlen(dmesg); i += 1024) {
            monitor_printf(rpc_data->mon, "%s", dmesg + i);
        }
        monitor_printf(rpc_data->mon, "\n");
    }

out:
    qemu_free(rpc_data->resp_xml);
    xmlrpc_DECREF(resp);
out_no_resp:
    rpc_data->mon_cb(rpc_data->mon_data, NULL);
    qemu_free(rpc_data);
}

/*
 * do_agent_viewdmesg(): View guest dmesg output
 */
int do_agent_viewdmesg(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    RPCRequest *rpc_data;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "(n)");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(RPCRequest));
    rpc_data->cb = do_agent_viewdmesg_cb;
    rpc_data->mon = mon;
    rpc_data->mon_cb = cb;
    rpc_data->mon_data = opaque;

    ret = rpc_execute(&env, "getdmesg", params, rpc_data);
    if (ret == -EREMOTE) {
        monitor_printf(mon, "RPC Failed (%i): %s\n", env.fault_code,
                       env.fault_string);
        return -1;
    } else if (ret == -1) {
        monitor_printf(mon, "RPC communication error\n");
        return -1;
    }

    return 0;
}
