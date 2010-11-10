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

#include "qemu_socket.h"
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
                       xmlrpc_value *params, VARPCData *rpc_data)
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

    rpc_data->send_req_xml = call_xml;

    ret = va_rpc_send_request(rpc_data, fd);
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

void do_agent_viewfile_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;
    const char *contents = NULL;
    int i;

    qdict = qobject_to_qdict(data);
    if (!qdict_haskey(qdict, "contents")) {
        return;
    }

    contents = qdict_get_str(qdict, "contents");
    if (contents != NULL) {
         /* monitor_printf truncates so do it in chunks. also, file_contents
          * may not be null-termed at proper location so explicitly calc
          * last chunk sizes */
        for (i = 0; i < strlen(contents); i += 1024) {
            monitor_printf(mon, "%.1024s", contents + i);
        }
    }
    monitor_printf(mon, "\n");
}

static void do_agent_viewfile_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    char *file_contents = NULL;
    size_t file_size;
    int ret;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
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
        qdict_put(qdict, "contents",
                  qstring_from_substr(file_contents, 0, file_size-1));
    }

out:
    xmlrpc_DECREF(resp);
out_no_resp:
    rpc_data->mon_cb(rpc_data->mon_data, QOBJECT(qdict));
    qobject_decref(QOBJECT(qdict));
}

/*
 * do_agent_viewfile(): View a text file in the guest
 */
int do_agent_viewfile(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    VARPCData *rpc_data;
    const char *filepath;
    int ret;

    filepath = qdict_get_str(mon_params, "filepath");
    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "(s)", filepath);
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_viewfile_cb;
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

void do_agent_viewdmesg_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;
    const char *contents = NULL;
    int i;

    qdict = qobject_to_qdict(data);
    if (!qdict_haskey(qdict, "contents")) {
        goto out;
    }

    contents = qdict_get_str(qdict, "contents");
    if (contents != NULL) {
         /* monitor_printf truncates so do it in chunks. also, file_contents
          * may not be null-termed at proper location so explicitly calc
          * last chunk sizes */
        for (i = 0; i < strlen(contents); i += 1024) {
            monitor_printf(mon, "%.1024s", contents + i);
        }
    }

out:
    monitor_printf(mon, "\n");
}

static void do_agent_viewdmesg_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    char *dmesg = NULL;
    int ret;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
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
        qdict_put(qdict, "contents", qstring_from_str(dmesg));
    }

out:
    xmlrpc_DECREF(resp);
out_no_resp:
    rpc_data->mon_cb(rpc_data->mon_data, QOBJECT(qdict));
    qobject_decref(QOBJECT(qdict));
}

/*
 * do_agent_viewdmesg(): View guest dmesg output
 */
int do_agent_viewdmesg(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    VARPCData *rpc_data;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "(n)");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_viewdmesg_cb;
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
