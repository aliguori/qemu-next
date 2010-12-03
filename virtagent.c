/*
 * virtagent - host/guest RPC client functions
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
#include "virtagent-common.h"

static VAClientData *va_client_data;

static void va_set_capabilities(QList *qlist)
{
    TRACE("called");

    if (va_client_data == NULL) {
        LOG("client is uninitialized, unable to set capabilities");
        return;
    }

    if (va_client_data->supported_methods != NULL) {
        qobject_decref(QOBJECT(va_client_data->supported_methods));
        va_client_data->supported_methods = NULL;
        TRACE("capabilities reset");
    }

    if (qlist != NULL) {
        va_client_data->supported_methods = qlist_copy(qlist);
        TRACE("capabilities set");
    }
}

typedef struct VACmpState {
    const char *method;
    bool found;
} VACmpState;

static void va_cmp_capability_iter(QObject *obj, void *opaque)
{
    QString *method = qobject_to_qstring(obj);
    const char *method_str = NULL;
    VACmpState *cmp_state = opaque;

    if (method) {
        method_str = qstring_get_str(method);
    }

    if (method_str && opaque) {
        if (strcmp(method_str, cmp_state->method) == 0) {
            cmp_state->found = 1;
        }
    }
}

static bool va_has_capability(const char *method)
{
    VACmpState cmp_state;

    if (method == NULL) {
        return false;
    }

    /* we can assume method introspection is available */
    if (strcmp(method, "system.listMethods") == 0) {
        return true;
    }
    /* assume hello is available to we can probe for/notify the host
     * rpc server
     */
    if (strcmp(method, "va.hello") == 0) {
        return true;
    }

    /* compare method against the last retrieved supported method list */
    cmp_state.method = method;
    cmp_state.found = false;
    if (va_client_data->supported_methods) {
        qlist_iter(va_client_data->supported_methods,
                   va_cmp_capability_iter,
                   (void *)&cmp_state);
    }

    return cmp_state.found;
}

int va_client_init(VAClientData *client_data)
{
    client_data->supported_methods = NULL;
    va_client_data = client_data;

    return 0;
}

static int va_rpc_has_error(xmlrpc_env *env)
{
    if (env->fault_occurred) {
        qerror_report(QERR_RPC_FAILED, env->fault_code, env->fault_string);
        return -1;
    }
    return 0;
}

static int va_do_rpc(xmlrpc_env *const env, const char *function,
                     xmlrpc_value *params, VAClientCallback *cb,
                     MonitorCompletion *mon_cb, void *mon_data)
{
    xmlrpc_mem_block *req_xml;
    int ret;

    if (!va_has_capability(function)) {
        LOG("guest agent does not have required capability");
        ret = -1;
        goto out;
    }

    req_xml = XMLRPC_MEMBLOCK_NEW(char, env, 0);
    xmlrpc_serialize_call(env, req_xml, function, params);
    if (va_rpc_has_error(env)) {
        ret = -EINVAL;
        goto out_free;
    }

    ret = va_client_job_add(req_xml, cb, mon_cb, mon_data);
    if (ret) {
        goto out_free;
    }

    return ret;
out_free:
    XMLRPC_MEMBLOCK_FREE(char, req_xml);
out:
    return ret;
}

/* QMP/HMP RPC client functions */

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

static void do_agent_viewfile_cb(const char *resp_data,
                                 size_t resp_data_len,
                                 MonitorCompletion *mon_cb,
                                 void *mon_data)
{
    xmlrpc_value *resp = NULL;
    char *file_contents = NULL;
    size_t file_size;
    int ret;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        ret = -1;
        goto out_no_resp;
    }

    xmlrpc_parse_value(&env, resp, "6", &file_contents, &file_size);
    if (va_rpc_has_error(&env)) {
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
    if (mon_cb) {
        mon_cb(mon_data, QOBJECT(qdict));
    }
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
    const char *filepath;
    int ret;

    filepath = qdict_get_str(mon_params, "filepath");
    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "(s)", filepath);
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    ret = va_do_rpc(&env, "va.getfile", params, do_agent_viewfile_cb, cb,
                    opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
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

static void do_agent_viewdmesg_cb(const char *resp_data,
                                  size_t resp_data_len,
                                  MonitorCompletion *mon_cb,
                                  void *mon_data)
{
    xmlrpc_value *resp = NULL;
    char *dmesg = NULL;
    int ret;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        ret = -1;
        goto out_no_resp;
    }

    xmlrpc_parse_value(&env, resp, "s", &dmesg);
    if (va_rpc_has_error(&env)) {
        ret = -1;
        goto out;
    }

    if (dmesg != NULL) {
        qdict_put(qdict, "contents", qstring_from_str(dmesg));
    }

out:
    xmlrpc_DECREF(resp);
out_no_resp:
    if (mon_cb) {
        mon_cb(mon_data, QOBJECT(qdict));
    }
}

/*
 * do_agent_viewdmesg(): View guest dmesg output
 */
int do_agent_viewdmesg(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "()");
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    ret = va_do_rpc(&env, "va.getdmesg", params, do_agent_viewdmesg_cb, cb,
                    opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
}

static void do_agent_shutdown_cb(const char *resp_data,
                                 size_t resp_data_len,
                                 MonitorCompletion *mon_cb,
                                 void *mon_data)
{
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;

    TRACE("called");

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        LOG("RPC Failed (%i): %s\n", env.fault_code,
            env.fault_string);
        goto out_no_resp;
    }

    xmlrpc_DECREF(resp);
out_no_resp:
    if (mon_cb) {
        mon_cb(mon_data, NULL);
    }
}

/*
 * do_agent_shutdown(): Shutdown a guest
 */
int do_agent_shutdown(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    const char *shutdown_type;
    int ret;

    TRACE("called");

    xmlrpc_env_init(&env);
    shutdown_type = qdict_get_str(mon_params, "shutdown_type");
    params = xmlrpc_build_value(&env, "(s)", shutdown_type);
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    ret = va_do_rpc(&env, "va.shutdown", params, do_agent_shutdown_cb, cb,
                    opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
}
