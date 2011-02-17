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

int va_client_init(VAManager *m, VAClientData *client_data)
{
    client_data->supported_methods = NULL;
    client_data->enabled = true;
    client_data->manager = m;
    va_client_data = client_data;

    return 0;
}

int va_client_close(void)
{
    va_client_data = NULL;
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

static bool va_is_enabled(void)
{
    return va_client_data && va_client_data->enabled;
}

typedef struct VAClientRequest {
    xmlrpc_mem_block *content;
    int magic;
    char tag[64];
    VAClientCallback *cb;
    /* for use by QMP functions */
    MonitorCompletion *mon_cb;
    void *mon_data;
    int timeout;
    QEMUTimer *timer;
} VAClientRequest;

typedef struct VAClientResponse {
    void *content;
    size_t content_len;
} VAClientResponse;

static void va_client_timeout(void *opaque)
{
    VAClientRequest *req = opaque;
    qemu_del_timer(req->timer);
    req->timer = NULL;
    va_client_job_cancel(va_client_data->manager, req->tag);
}

/* called by xport layer to indicate send completion to VAManager */
static void va_send_request_cb(const void *opaque)
{
    const char *tag = opaque;
    va_client_job_send_done(va_client_data->manager, tag);
}

/* called by VAManager to start send, in turn calls out to xport layer */
static int va_send_request(void *opaque, const char *tag)
{
    VAClientRequest *req = opaque;
    int ret = va_xport_send_request(XMLRPC_MEMBLOCK_CONTENTS(char, req->content),
                                    XMLRPC_MEMBLOCK_SIZE(char, req->content),
                                    tag, tag, va_send_request_cb);
    /* register timeout */
    if (req->timeout) {
        req->timer = qemu_new_timer(rt_clock, va_client_timeout, req);
        qemu_mod_timer(req->timer, qemu_get_clock(rt_clock) + req->timeout);
    }
    return ret;
}

/* called by xport layer to pass response to VAManager */
void va_client_read_response_done(void *content, size_t content_len, const char *tag)
{
    VAClientResponse *resp = qemu_mallocz(sizeof(VAClientResponse));
    resp->content = content;
    resp->content_len = content_len;
    va_client_job_read_done(va_client_data->manager, tag, resp);
}

/* called by VAManager once RPC response is recieved */
static int va_callback(void *opaque, void *resp_opaque, const char *tag)
{
    VAClientRequest *req = opaque; 
    VAClientResponse *resp = resp_opaque;
    TRACE("called");
    if (req->timer) {
        qemu_del_timer(req->timer);
    }
    if (req->cb) {
        if (resp) {
            req->cb(resp->content, resp->content_len, req->mon_cb, req->mon_data);
        } else {
            /* RPC did not complete */
            req->cb(NULL, 0, req->mon_cb, req->mon_data);
        }
    }
    /* TODO: cleanup */
    if (req) {
        if (req->content) {
            XMLRPC_MEMBLOCK_FREE(char, req->content);
        }
        qemu_free(req);
    }
    if (resp) {
        if (resp->content) {
            qemu_free(resp->content);
        }
        qemu_free(resp);
    }
    return 0;
}

static VAClientJobOps client_job_ops = {
    .send = va_send_request,
    .callback = va_callback,
};

static int va_do_rpc(xmlrpc_env *const env, const char *function,
                     xmlrpc_value *params, VAClientCallback *cb,
                     MonitorCompletion *mon_cb, void *mon_data)
{
    xmlrpc_mem_block *req_xml;
    VAClientRequest *req;
    struct timeval ts;
    int ret;

    if (!va_is_enabled()) {
        LOG("virtagent not initialized");
        ret = -ENOTCONN;
    }

    if (!va_has_capability(function)) {
        LOG("guest agent does not have required capability");
        ret = -ENOSYS;
        goto out;
    }

    req_xml = XMLRPC_MEMBLOCK_NEW(char, env, 0);
    xmlrpc_serialize_call(env, req_xml, function, params);
    if (va_rpc_has_error(env)) {
        ret = -EINVAL;
        goto out_free;
    }

    req = qemu_mallocz(sizeof(VAClientRequest));
    req->content = req_xml;
    req->cb = cb;
    req->mon_cb = mon_cb;
    req->mon_data = mon_data;
    req->timeout = VA_CLIENT_TIMEOUT_MS;
    req->magic = 9999;
    /* TODO: should switch to UUIDs eventually */
    memset(req->tag, 0, 64);
    gettimeofday(&ts, NULL);
    sprintf(req->tag, "%u.%u", (uint32_t)ts.tv_sec, (uint32_t)ts.tv_usec);
    TRACE("req->content: %p, req->cb: %p, req->mon_cb: %p, req->mon_data: %p",
          req->content, req->cb, req->mon_cb, req->mon_data);

    ret = va_client_job_add(va_client_data->manager, req->tag, req,
                            client_job_ops);
    if (ret) {
        qemu_free(req);
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

void do_agent_ping_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;
    const char *response;

    TRACE("called");

    qdict = qobject_to_qdict(data);
    response = qdict_get_str(qdict, "response");
    if (qdict_haskey(qdict, "response")) {
        monitor_printf(mon, "%s", response);
    }

    monitor_printf(mon, "\n");
}

static void do_agent_ping_cb(const char *resp_data,
                                     size_t resp_data_len,
                                     MonitorCompletion *mon_cb,
                                     void *mon_data)
{
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    TRACE("called");

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        qdict_put(qdict, "response", qstring_from_str("error"));
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        qdict_put(qdict, "response", qstring_from_str("error"));
        goto out_no_resp;
    }
    qdict_put(qdict, "response", qstring_from_str("ok"));

    xmlrpc_DECREF(resp);
out_no_resp:
    if (mon_cb) {
        mon_cb(mon_data, QOBJECT(qdict));
    }
    qobject_decref(QOBJECT(qdict));
}

/*
 * do_agent_ping(): Ping a guest
 */
int do_agent_ping(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "(n)");
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    ret = va_do_rpc(&env, "va.ping", params, do_agent_ping_cb, cb, opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
}

static void va_print_capability_iter(QObject *obj, void *opaque)
{
    Monitor *mon = opaque;
    QString *method = qobject_to_qstring(obj);
    const char *method_str;

    if (method) {
        method_str = qstring_get_str(method);
        monitor_printf(mon, "%s\n", method_str); 
    }
}

void do_agent_capabilities_print(Monitor *mon, const QObject *data)
{
    QList *qlist;

    TRACE("called");

    monitor_printf(mon, "the following RPC methods are supported by the guest agent:\n");
    qlist = qobject_to_qlist(data);
    qlist_iter(qlist, va_print_capability_iter, mon);
}

static void do_agent_capabilities_cb(const char *resp_data,
                                     size_t resp_data_len,
                                     MonitorCompletion *mon_cb,
                                     void *mon_data)
{
    xmlrpc_value *resp = NULL;
    xmlrpc_value *cur_val = NULL;
    const char *cur_method = NULL;
    xmlrpc_env env;
    QList *qlist = qlist_new();
    int i;

    TRACE("called");

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    TRACE("resp = %s\n", resp_data);

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        goto out_no_resp;
    }

    /* extract the list of supported RPCs */
    for (i = 0; i < xmlrpc_array_size(&env, resp); i++) {
        xmlrpc_array_read_item(&env, resp, i, &cur_val);
        xmlrpc_read_string(&env, cur_val, &cur_method);
        if (cur_method) {
            TRACE("cur_method: %s", cur_method);
            qlist_append_obj(qlist, QOBJECT(qstring_from_str(cur_method)));
        }
        xmlrpc_DECREF(cur_val);
    }

    /* set our client capabilities accordingly */
    va_set_capabilities(qlist);

    xmlrpc_DECREF(resp);
out_no_resp:
    if (mon_cb) {
        TRACE("CALLING MONITOR CALLBACK");
        mon_cb(mon_data, QOBJECT(qlist));
    } else {
        TRACE("NOT CALLING MONITOR CALLBACK");
    }
    qobject_decref(QOBJECT(qlist));
}

/*
 * do_agent_capabilities(): Fetch/re-negotiate guest agent capabilities
 */
int do_agent_capabilities(Monitor *mon, const QDict *mon_params,
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

    ret = va_do_rpc(&env, "system.listMethods", params,
                    do_agent_capabilities_cb, cb, opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
}

/* non-HMP/QMP RPC client functions */

int va_client_init_capabilities(void)
{
    xmlrpc_env env;
    xmlrpc_value *params;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "()");
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    return va_do_rpc(&env, "system.listMethods", params,
                     do_agent_capabilities_cb, NULL, NULL);
}

static void va_send_hello_cb(const char *resp_data,
                             size_t resp_data_len,
                             MonitorCompletion *mon_cb,
                             void *mon_data)
{
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;

    TRACE("called");

    if (resp_data == NULL) {
        LOG("error handling RPC request");
        return;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, resp_data, resp_data_len);
    if (va_rpc_has_error(&env)) {
        LOG("error parsing RPC response");
        return;
    }

    xmlrpc_DECREF(resp);
}

int va_send_hello(void)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    int ret;

    TRACE("called");

    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "()");
    if (va_rpc_has_error(&env)) {
        return -1;
    }

    ret = va_do_rpc(&env, "va.hello", params, va_send_hello_cb, NULL, NULL);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(ret));
    }
    xmlrpc_DECREF(params);
    return ret;
}
