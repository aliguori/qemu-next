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
#include "qjson.h"
#include "qint.h"
#include "monitor.h"
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

static void va_set_version_level(const char *version) {
    if (version) {
        pstrcpy(va_client_data->guest_version, 32, version);
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

    /* we can assume capabilities is available */
    if (strcmp(method, "capabilities") == 0) {
        return true;
    }
    /* assume hello is available to we can probe for/notify the host
     * rpc server
     */
    if (strcmp(method, "hello") == 0) {
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

static bool va_is_enabled(void)
{
    return va_client_data && va_client_data->enabled;
}

typedef struct VAClientRequest {
    QString *payload;
    char tag[64];
    VAClientCallback *cb;
    /* for use by QMP functions */
    MonitorCompletion *mon_cb;
    void *mon_data;
    int timeout;
    QEMUTimer *timer;
} VAClientRequest;

typedef struct VAClientResponse {
    char *content;
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
    const char *payload_json;
    int ret;

    TRACE("called");
    if (!req || !req->payload) {
        TRACE("marker");
        return -EINVAL;
    }
    payload_json = qstring_get_str(req->payload);
    if (!payload_json) {
        TRACE("marker");
        return -EINVAL;
    }
    TRACE("marker");
    ret = va_xport_send_request(payload_json, strlen(payload_json),
                                tag, tag, va_send_request_cb);
    TRACE("marker");
    /* register timeout */
    if (req->timeout) {
        TRACE("marker");
        req->timer = qemu_new_timer(rt_clock, va_client_timeout, req);
        qemu_mod_timer(req->timer, qemu_get_clock(rt_clock) + req->timeout);
    }
    TRACE("marker");
    return ret;
}

/* called by xport layer to pass response to VAManager */
void va_client_read_response_done(const char *content, size_t content_len, const char *tag)
{
    QDict *resp = NULL;
    QObject *resp_qobject;

    resp_qobject = qobject_from_json(content);
    if (resp_qobject) {
        resp = qobject_to_qdict(resp_qobject);
    }
    va_client_job_read_done(va_client_data->manager, tag, resp);
}

/* called by VAManager once RPC response is recieved */
static int va_callback(void *opaque, void *resp_opaque, const char *tag)
{
    VAClientRequest *req = opaque; 
    QDict *resp = resp_opaque;

    TRACE("called");

    if (req->timer) {
        qemu_del_timer(req->timer);
    }

    if (req->cb) {
        if (resp) {
            req->cb(resp, req->mon_cb, req->mon_data);
        } else {
            /* RPC did not complete */
            req->cb(NULL, req->mon_cb, req->mon_data);
        }
    }

    if (req) {
        if (req->payload) {
            QDECREF(req->payload);
        }
        qemu_free(req);
    }

    if (resp) {
        QDECREF(resp);
    }

    return 0;
}

static VAClientJobOps client_job_ops = {
    .send = va_send_request,
    .callback = va_callback,
};

static int va_do_rpc(const char *method, const QDict *params,
                     VAClientCallback *cb, MonitorCompletion *mon_cb,
                     void *mon_data)
{
    VAClientRequest *req;
    QDict *payload, *params_copy = NULL;
    QString *payload_json;
    struct timeval ts;
    int ret;

    if (!va_is_enabled()) {
        LOG("virtagent not initialized");
        ret = -ENOTCONN;
    }

    if (!va_has_capability(method)) {
        LOG("guest agent does not have required capability: %s", method);
        ret = -ENOTSUP;
        goto out;
    }

    req = qemu_mallocz(sizeof(VAClientRequest));
    req->cb = cb;
    req->mon_cb = mon_cb;
    req->mon_data = mon_data;
    req->timeout = VA_CLIENT_TIMEOUT_MS;

    /* add params and remote RPC method to call to payload */
    payload = qdict_new();
    qdict_put_obj(payload, "method",
                  QOBJECT(qstring_from_str(method)));
    if (params) {
        params_copy = va_qdict_copy(params);
        if (!params_copy) {
            LOG("error processing parameters");
            QDECREF(payload);
            ret = -EINVAL;
            goto out_free;
        }
        qdict_put_obj(payload, "params", QOBJECT(params_copy));
    }

    /* convert payload to json */
    payload_json = qobject_to_json(QOBJECT(payload));
    QDECREF(payload);
    if (!payload_json) {
        LOG("error converting request to json");
        ret = -EINVAL;
        goto out_free;
    }
    req->payload = payload_json;

    /* TODO: should switch to UUIDs eventually */
    memset(req->tag, 0, 64);
    gettimeofday(&ts, NULL);
    sprintf(req->tag, "%u.%u", (uint32_t)ts.tv_sec, (uint32_t)ts.tv_usec);
    TRACE("req->payload: %p, req->cb: %p, req->mon_cb: %p, req->mon_data: %p",
          req->payload, req->cb, req->mon_cb, req->mon_data);

    ret = va_client_job_add(va_client_data->manager, req->tag, req,
                            client_job_ops);
    if (ret) {
        TRACE("marker");
        va_client_job_cancel(va_client_data->manager, req->tag);
        goto out_free;
    }

out:
    return ret;
out_free:
    qemu_free(req);
    return ret;
}

/* validate the RPC response. if response indicates an error, log it
 * to stderr/monitor. if return_data != NULL, return_data will be set
 * to the response payload of the RPC if present, otherwise an error
 * will be logged. if return_data == NULL, response payload is ignored,
 * and only the RPC's error indicator is checked for success.
 *
 * XXX: The JSON that generates the response may originate from untrusted
 * sources such as an unsupported/malicious guest agent, so we must take
 * particular care to not make any assumptions about what the response
 * contains. In particular, always check for key existence, and no blind
 * qdict_get_<type>() calls since the value may be an unexpected type. This
 * also applies to the return_data we pass back to callers.
 */
static bool va_check_response_ok(QDict *resp, QDict **return_data)
{
    int errnum;
    const char *errstr = NULL;

    TRACE("called");
    /* TODO: not sure if errnum is of much use here */
    if (!resp) {
        errnum = ENOMSG;
        errstr = "response is null";
        goto out_bad;
    }
    
    if (va_qdict_haskey_with_type(resp, "errnum", QTYPE_QINT)) {
        errnum = qdict_get_int(resp, "errnum");
        if (errnum) {
            if (va_qdict_haskey_with_type(resp, "errstr", QTYPE_QSTRING)) {
                errstr = qdict_get_str(resp, "errstr");
            }
            goto out_bad;
        }
    } else {
        errnum = EINVAL;
        errstr = "response is missing error code";
        goto out_bad;
    }
    
    if (return_data) {
        if (va_qdict_haskey_with_type(resp, "return_data", QTYPE_QDICT)) {
            TRACE("marker");
            *return_data = qdict_get_qdict(resp, "return_data");
        } else {
            errnum = EINVAL;
            errstr = "response indicates success, but missing expected retval";
            goto out_bad;
        }
    }

    return true;
out_bad:
    qerror_report(QERR_RPC_FAILED, errnum, errstr);
    return false;
}

/* QMP/HMP RPC client functions and their helpers */

static void va_print_capability_iter(QObject *obj, void *opaque)
{
    Monitor *mon = opaque;
    QString *function = qobject_to_qstring(obj);
    const char *function_str;

    if (function) {
        function_str = qstring_get_str(function);
        monitor_printf(mon, "%s\n", function_str); 
    }
}

void do_va_capabilities_print(Monitor *mon, const QObject *data)
{
    QDict *ret = qobject_to_qdict(data);

    TRACE("called");
    if (!data) {
        return;
    }

    monitor_printf(mon,
                   "guest agent version: %s\n"
                   "supported methods:\n", qdict_get_str(ret, "version"));
    qlist_iter(qdict_get_qlist(ret, "methods"), va_print_capability_iter, mon);
}

static void do_va_capabilities_cb(QDict *resp,
                                  MonitorCompletion *mon_cb,
                                  void *mon_data)
{
    QDict *ret = NULL;
    QObject *ret_qobject = NULL;
        
    TRACE("called");
    if (!va_check_response_ok(resp, &ret)) {
        goto out;
    }

    if (!va_qdict_haskey_with_type(ret, "methods", QTYPE_QLIST) ||
        !va_qdict_haskey_with_type(ret, "version", QTYPE_QSTRING)) {
        qerror_report(QERR_VA_FAILED, -EINVAL,
                      "response does not contain required fields");
        goto out;
    }
    va_set_capabilities(qdict_get_qlist(ret, "methods"));
    va_set_version_level(qdict_get_str(ret, "version"));
    ret_qobject = QOBJECT(ret);
out:
    if (mon_cb) {
        mon_cb(mon_data, ret_qobject);
    }
}

/*
 * do_va_capabilities(): Fetch/re-negotiate guest agent capabilities
 */
int do_va_capabilities(Monitor *mon, const QDict *params,
                       MonitorCompletion cb, void *opaque)
{
    int ret = va_do_rpc("capabilities", params, do_va_capabilities_cb, cb,
                        opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(-ret));
    }
    return ret;
}

void do_va_ping_print(Monitor *mon, const QObject *data)
{
    QDict *ret = qobject_to_qdict(data);

    TRACE("called");

    if (!data) {
        return;
    }
    monitor_printf(mon, "status: %s\n", qdict_get_str(ret, "status"));
}

static void do_va_ping_cb(QDict *resp,
                          MonitorCompletion *mon_cb,
                          void *mon_data)
{
    QDict *ret = qdict_new();
    const char *status;
        
    if (va_check_response_ok(resp, NULL)) {
        status = "success";
    } else {
        status = "error or timeout";
    }
    qdict_put_obj(ret, "status", QOBJECT(qstring_from_str(status)));

    if (mon_cb) {
        mon_cb(mon_data, QOBJECT(ret));
    }
    QDECREF(ret);
}

/*
 * do_va_ping(): Ping the guest agent
 */
int do_va_ping(Monitor *mon, const QDict *params,
               MonitorCompletion cb, void *opaque)
{
    int ret = va_do_rpc("ping", params, do_va_ping_cb, cb, opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(-ret));
    }
    return ret;
}

static void do_va_shutdown_cb(QDict *resp,
                              MonitorCompletion *mon_cb,
                              void *mon_data)
{
    TRACE("called");
    va_check_response_ok(resp, NULL); 
    if (mon_cb) {
        mon_cb(mon_data, NULL);
    }
}

/*
 * do_va_shutdown(): shutdown/powerdown/reboot the guest
 */
int do_va_shutdown(Monitor *mon, const QDict *params,
                   MonitorCompletion cb, void *opaque)
{
    int ret = va_do_rpc("shutdown", params, do_va_shutdown_cb, cb, opaque);
    if (ret) {
        qerror_report(QERR_VA_FAILED, ret, strerror(-ret));
    }
    return ret;
}

/* RPC client functions called outside of HMP/QMP */

int va_client_init_capabilities(void)
{
    int ret = va_do_rpc("capabilities", NULL, do_va_capabilities_cb, NULL,
                        NULL);
    if (ret) {
        LOG("erroring negotiating capabilities: %s", strerror(-ret));
    }

    return 0;
}

int va_send_hello(void)
{
    int ret = va_do_rpc("hello", NULL, NULL, NULL, NULL);
    if (ret) {
        LOG("error sending start up notification to host: %s",
            strerror(-ret));
    }
    return ret;
}
