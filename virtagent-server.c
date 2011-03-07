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
#include "virtagent-common.h"
#include "qemu_socket.h"
#include "qjson.h"
#include "qint.h"

static VARPCFunction guest_functions[];
static VARPCFunction host_functions[];
static VAServerData *va_server_data;
static bool va_enable_syslog = false; /* enable syslog'ing of RPCs */

#define SLOG(msg, ...) do { \
    char msg_buf[1024]; \
    if (!va_enable_syslog) { \
        break; \
    } \
    snprintf(msg_buf, 1024, msg, ## __VA_ARGS__); \
    syslog(LOG_INFO, "virtagent, %s", msg_buf); \
} while(0)

/* helper functions for RPCs */

static QDict *va_server_format_response(QDict *return_data, int errnum,
                                        const char *errstr)
{
    QDict *response = qdict_new();

    if (errnum == -1) {
        if (!errstr) {
            errstr = "unknown remote error handling RPC";
        }
    }
    if (errstr) {
        qdict_put_obj(response, "errstr",
                      QOBJECT(qstring_from_str(errstr)));
    }
    qdict_put_obj(response, "errnum", QOBJECT(qint_from_int(errnum)));
    if (return_data) {
        qdict_put_obj(response, "return_data", QOBJECT(return_data));
    }

    return response;
}

/* RPCs */

/* va_shutdown(): initiate guest shutdown
 * params/response qdict format:
 *   params{shutdown_mode}: "reboot"|"powerdown"|"shutdown"
 *   response{error}: <error code>
 *   response{errstr}: <error description>
 */
static QDict *va_shutdown(const QDict *params)
{
    int ret;
    const char *shutdown_mode, *shutdown_flag;

    shutdown_mode = qdict_get_try_str(params, "shutdown_mode");
    SLOG("va_shutdown(), shutdown_mode:%s", shutdown_mode);

    if (!shutdown_mode) {
        ret = -EINVAL;
        LOG("missing shutdown argument");
        goto out;
    } else if (strcmp(shutdown_mode, "halt") == 0) {
        shutdown_flag = "-H";
    } else if (strcmp(shutdown_mode, "powerdown") == 0) {
        shutdown_flag = "-P";
    } else if (strcmp(shutdown_mode, "reboot") == 0) {
        shutdown_flag = "-r";
    } else {
        ret = -EINVAL;
        LOG("invalid shutdown argument");
        goto out;
    }

    ret = fork();
    if (ret == 0) {
        /* child, start the shutdown */
        setsid();
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);

        sleep(5);
        ret = execl("/sbin/shutdown", "shutdown", shutdown_flag, "+0",
                    "hypervisor initiated shutdown", (char*)NULL);
        if (ret < 0) {
            LOG("execl() failed: %s", strerror(errno));
            exit(1);
        }
        exit(0);
    } else if (ret < 0) {
        LOG("fork() failed: %s", strerror(errno));
    } else {
        ret = 0;
    }

out:
    return va_server_format_response(NULL, ret, strerror(errno));
}

/* va_hello(): handle client startup notification
 * params/response qdict format (*=optional):
 *   response{error}: <error code>
 *   response{errstr}: <error description>
 */
static QDict *va_hello(const QDict *params)
{
    int ret;
    TRACE("called");
    SLOG("va_hello()");
    ret = va_client_init_capabilities();
    if (ret < 0) {
        LOG("error setting initializing client capabilities");
    }
    return va_server_format_response(NULL, 0, NULL);
}

/* va_ping(): respond to/pong to client.
 * params/response qdict format (*=optional):
 *   response{error}: <error code>
 *   response{errstr}: <error description>
 */
static QDict *va_ping(const QDict *params)
{
    TRACE("called");
    SLOG("va_ping()");
    return va_server_format_response(NULL, 0, NULL);
}

/* va_capabilities(): return server capabilities
 * params/response qdict format (*=optional):
 *   response{error}: <error code>
 *   response{errstr}: <error description>
 *   response{return_data}{methods}: list of callable RPCs
 *   response{return_data}{version}: virtagent version
 */
static QDict *va_capabilities(const QDict *params)
{
    QList *functions = qlist_new();
    QDict *ret = qdict_new();
    int i;
    const char *func_name;

    TRACE("called");
    SLOG("va_capabilities()");

    for (i = 0; va_server_data->functions[i].func != NULL; ++i) {
        func_name = va_server_data->functions[i].func_name;
        qlist_append_obj(functions, QOBJECT(qstring_from_str(func_name)));
    }
    qdict_put_obj(ret, "methods", QOBJECT(functions));
    qdict_put_obj(ret, "version", QOBJECT(qstring_from_str(VA_VERSION)));

    return va_server_format_response(ret, 0, NULL);
}

static VARPCFunction guest_functions[] = {
    { .func = va_shutdown,
      .func_name = "shutdown" },
    { .func = va_ping,
      .func_name = "ping" },
    { .func = va_capabilities,
      .func_name = "capabilities" },
    { NULL, NULL }
};

static VARPCFunction host_functions[] = {
    { .func = va_ping,
      .func_name = "ping" },
    { .func = va_hello,
      .func_name = "hello" },
    { NULL, NULL }
};

static bool va_server_is_enabled(void)
{
    return va_server_data && va_server_data->enabled;
}

typedef struct VARequestData {
    QDict *request;
    QString *response;
} VARequestData;

static int va_do_server_rpc(VARequestData *d, const char *tag)
{
    int ret = 0, i;
    const char *func_name;
    VARPCFunction *func_list = va_server_data->is_host ?
                             host_functions : guest_functions;
    QDict *response = NULL, *params = NULL;
    bool found;

    TRACE("called");

    if (!va_server_is_enabled()) {
        ret = -EBUSY;
        goto out;
    }

    if (!d->request) {
        ret = -EINVAL;
        goto out;
    }

    if (!va_qdict_haskey_with_type(d->request, "method", QTYPE_QSTRING)) {
        ret = -EINVAL;
        va_server_job_cancel(va_server_data->manager, tag);
        goto out;
    }
    func_name = qdict_get_str(d->request, "method");
    for (i = 0; func_list[i].func != NULL; ++i) {
        if (strcmp(func_name, func_list[i].func_name) == 0) {
            if (va_qdict_haskey_with_type(d->request, "params", QTYPE_QDICT)) {
                params = qdict_get_qdict(d->request, "params");
            }
            response = func_list[i].func(params);
            found = true;
            break;
        }
    }

    if (!response) {
        if (found) {
            response = va_server_format_response(NULL, -1,
                                                 "error executing rpc");
        } else {
            response = va_server_format_response(NULL, -1,
                                                 "unsupported rpc specified");
        }
    }
    /* TODO: store the json rather than the QDict that generates it */
    d->response = qobject_to_json(QOBJECT(response));
    if (!d->response) {
        ret = -EINVAL;
        goto out;
    }

    va_server_job_execute_done(va_server_data->manager, tag);

out:
    return ret;
}

int va_server_init(VAManager *m, VAServerData *server_data, bool is_host)
{
    va_enable_syslog = !is_host; /* enable logging for guest agent */
    server_data->functions = is_host ? host_functions : guest_functions;
    server_data->enabled = true;
    server_data->is_host = is_host;
    server_data->manager = m;
    va_server_data = server_data;

    return 0;
}

int va_server_close(void)
{
    if (va_server_data != NULL) {
        va_server_data = NULL;
    }
    return 0;
}

/* called by VAManager to start executing the RPC */
static int va_execute(void *opaque, const char *tag)
{
    VARequestData *d = opaque;
    int ret = va_do_server_rpc(d, tag);
    if (ret < 0) {
        LOG("error occurred executing RPC: %s", strerror(-ret));
    }

    return ret;
}

/* called by xport layer to indicate send completion to VAManager */
static void va_send_response_cb(const void *opaque)
{
    const char *tag = opaque;
    va_server_job_send_done(va_server_data->manager, tag);
}

/* called by VAManager to start send, in turn calls out to xport layer */
static int va_send_response(void *opaque, const char *tag)
{
    VARequestData *d = opaque;
    const char *json_resp;
    int ret;
   
    TRACE("called, request data d: %p", opaque);
    if (!d->response) {
        LOG("server generated null response");
        ret = -EINVAL;
        goto out_cancel;
    }
    json_resp = qstring_get_str(d->response);
    if (!json_resp) {
        ret = -EINVAL;
        LOG("server generated invalid JSON response");
        goto out_cancel;
    }

    ret = va_xport_send_response(json_resp, strlen(json_resp),
                                 tag, tag, va_send_response_cb);
    return ret;
out_cancel:
    va_server_job_cancel(va_server_data->manager, tag);
    return ret;
}

static int va_cleanup(void *opaque, const char *tag)
{
    VARequestData *d = opaque;
    if (d) {
        if (d->request) {
            QDECREF(d->request);
        }
        if (d->response) {
            QDECREF(d->response);
        }
        qemu_free(d);
    }
    return 0;
}

static VAServerJobOps server_job_ops = {
    .execute = va_execute,
    .send = va_send_response,
    .callback = va_cleanup,
};

/* create server jobs from requests read from xport layer */
int va_server_job_create(const char *content, size_t content_len, const char *tag)
{
    VARequestData *d = qemu_mallocz(sizeof(VAServerData));
    QObject *request_obj;

    if (!content) {
        LOG("recieved job with null request string");
        goto out_bad;
    }

    request_obj = qobject_from_json(content);
    if (!request_obj) {
        LOG("unable to parse JSON arguments");
        goto out_bad;
    }

    d->request = qobject_to_qdict(request_obj);
    if (!d->request) {
        LOG("recieved qobject of unexpected type: %d",
             qobject_type(request_obj));
        goto out_bad_free;
    }

    if (!va_qdict_haskey_with_type(d->request, "method", QTYPE_QSTRING)) {
        LOG("RPC command not specified");
        goto out_bad_free;
    }

    va_server_job_add(va_server_data->manager, tag, d, server_job_ops);

    return 0;
out_bad_free:
    if (d->request) {
        QDECREF(d->request);
    }
    qemu_free(d);
out_bad:
    return -EINVAL;
}
