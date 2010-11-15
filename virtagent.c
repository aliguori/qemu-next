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

typedef struct VARPCClientState {
    VPDriver *vp;
    const char *socket_path;
    QList *supported_methods;
} VARPCClientState;

/* only one virtagent client can exist at a time */
static VARPCClientState *client_state;

static int va_client_ready(void)
{
    if (client_state != NULL && client_state->vp != NULL
        && client_state->socket_path != NULL) {
        return 0;
    }

    return -1;
}

static void va_set_capabilities(QList *qlist)
{
    TRACE("called");

    if (client_state == NULL) {
        LOG("client is uninitialized, unable to set capabilities");
        return;
    }

    if (client_state->supported_methods != NULL) {
        qobject_decref(QOBJECT(client_state->supported_methods));
        client_state->supported_methods = NULL;
        TRACE("capabilities reset");
    }

    if (qlist != NULL) {
        client_state->supported_methods = qlist_copy(qlist);
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

    /* compare method against the last retrieved supported method list */
    cmp_state.method = method;
    cmp_state.found = false;
    if (client_state->supported_methods) {
        qlist_iter(client_state->supported_methods,
                   va_cmp_capability_iter,
                   (void *)&cmp_state);
    }

    return cmp_state.found;
}

int va_client_init(VPDriver *vp_drv, bool is_host)
{
    const char *service_id, *path;
    QemuOpts *opts;
    int fd, ret;

    if (client_state) {
        LOG("virtagent client already initialized");
        return -1;
    }
    client_state = qemu_mallocz(sizeof(VARPCClientState));
    client_state->vp = vp_drv;

    /* setup oforwards to connect to to send RPCs. if we're the host, we
     * want to connect to the guest agent service using the guest agent
     * client path, and vice-versa */
    service_id = !is_host ? HOST_AGENT_SERVICE_ID : GUEST_AGENT_SERVICE_ID;
    /* TODO: host agent path needs to be made unique amongst multiple
     * qemu instances
     */
    path = !is_host ? HOST_AGENT_PATH_CLIENT : GUEST_AGENT_PATH_CLIENT;
    client_state->socket_path = path;

    /* setup listening socket to forward connections over */
    opts = qemu_opts_create(qemu_find_opts("net"), "va_client_opts", 0);
    qemu_opt_set(opts, "path", path);
    fd = unix_listen_opts(opts);
    qemu_opts_del(opts);
    if (fd < 0) {
        LOG("error setting up listening socket");
        goto out_bad;
    }

    /* tell virtproxy to forward connections to this socket to
     * virtagent service on other end
     */
    ret = vp_set_oforward(vp_drv, fd, service_id);
    if (ret < 0) {
        LOG("error setting up virtproxy iforward");
        goto out_bad;
    }

    return 0;
out_bad:
    qemu_free(client_state);
    client_state = NULL;
    return -1;
}

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
    int fd = unix_connect(client_state->socket_path);
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

    ret = va_client_ready();
    if (ret < 0) {
        LOG("client in uninitialized state, unable to execute RPC");
        ret = -1;
        goto out;
    }

    if (!va_has_capability(function)) {
        LOG("guest agent does not have required capability");
        ret = -1;
        goto out;
    }

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

static void do_agent_shutdown_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;

    TRACE("called");

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
        LOG("RPC Failed (%i): %s\n", env.fault_code,
            env.fault_string);
        goto out_no_resp;
    }

    xmlrpc_DECREF(resp);
out_no_resp:
    rpc_data->mon_cb(rpc_data->mon_data, NULL);
}

/*
 * do_agent_shutdown(): Shutdown a guest
 */
int do_agent_shutdown(Monitor *mon, const QDict *mon_params,
                      MonitorCompletion cb, void *opaque)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    VARPCData *rpc_data;
    const char *shutdown_type;
    int ret;

    TRACE("called");

    xmlrpc_env_init(&env);
    shutdown_type = qdict_get_str(mon_params, "shutdown_type");
    params = xmlrpc_build_value(&env, "(s)", shutdown_type);
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_shutdown_cb;
    rpc_data->mon_cb = cb;
    rpc_data->mon_data = opaque;

    ret = rpc_execute(&env, "va_shutdown", params, rpc_data);
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

static void do_agent_ping_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;
    QDict *qdict = qdict_new();

    TRACE("called");

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
        qdict_put(qdict, "response", qstring_from_str("error"));
        goto out_no_resp;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
        qdict_put(qdict, "response", qstring_from_str("error"));
        goto out_no_resp;
    }
    qdict_put(qdict, "response", qstring_from_str("ok"));

    xmlrpc_DECREF(resp);
out_no_resp:
    if (rpc_data->mon_cb) {
        rpc_data->mon_cb(rpc_data->mon_data, QOBJECT(qdict));
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
    VARPCData *rpc_data;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "(n)");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_ping_cb;
    rpc_data->mon_cb = cb;
    rpc_data->mon_data = opaque;

    ret = rpc_execute(&env, "va_ping", params, rpc_data);
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

static void do_agent_capabilities_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    xmlrpc_value *cur_val = NULL;
    const char *cur_method = NULL;
    xmlrpc_env env;
    QList *qlist = qlist_new();
    int i;

    TRACE("called");

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
        goto out_no_resp;
    }

    TRACE("resp = %s\n", rpc_data->resp_xml);

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
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
    if (rpc_data->mon_cb) {
        rpc_data->mon_cb(rpc_data->mon_data, QOBJECT(qlist));
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
    VARPCData *rpc_data;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "()");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_capabilities_cb;
    rpc_data->mon_cb = cb;
    rpc_data->mon_data = opaque;

    ret = rpc_execute(&env, "system.listMethods", params, rpc_data);
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

/* non-HMP/QMP RPC client functions */

int va_client_init_capabilities(void)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    VARPCData *rpc_data;
    int ret;

    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "()");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = do_agent_capabilities_cb;
    rpc_data->mon_cb = NULL;
    rpc_data->mon_data = NULL;

    ret = rpc_execute(&env, "system.listMethods", params, rpc_data);
    if (ret == -EREMOTE) {
        LOG("RPC Failed (%i): %s\n", env.fault_code,
            env.fault_string);
        return -1;
    } else if (ret == -1) {
        LOG("RPC communication error\n");
        return -1;
    }

    return 0;
}

static void va_send_hello_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    xmlrpc_value *resp = NULL;
    xmlrpc_env env;

    TRACE("called");

    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error handling RPC request");
        return;
    }

    xmlrpc_env_init(&env);
    resp = xmlrpc_parse_response(&env, rpc_data->resp_xml,
                                 rpc_data->resp_xml_len);
    if (rpc_has_error(&env)) {
        LOG("error parsing RPC response");
        return;
    }

    xmlrpc_DECREF(resp);
}

int va_send_hello(void)
{
    xmlrpc_env env;
    xmlrpc_value *params;
    VARPCData *rpc_data;
    int ret;

    TRACE("called");

    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "(s)", "dummy");
    if (rpc_has_error(&env)) {
        return -1;
    }

    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = va_send_hello_cb;

    ret = rpc_execute(&env, "va_hello", params, rpc_data);
    if (ret == -EREMOTE) {
        LOG("RPC Failed (%i): %s", env.fault_code,
            env.fault_string);
        return -1;
    } else if (ret == -1) {
        LOG("RPC communication error");
        return -1;
    }

    return 0;
}
