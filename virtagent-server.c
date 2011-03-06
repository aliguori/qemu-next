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
#include "qjson.h"
#include "qint.h"

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

/* RPC functions common to guest/host daemons */

/* va_getfile(): return file contents
 * rpc return values:
 *   - base64-encoded file contents
 */
#if 0
static xmlrpc_value *va_getfile(xmlrpc_env *env,
                                xmlrpc_value *params,
                                void *user_data)
{
    const char *path;
    char *file_contents = NULL;
    char buf[VA_FILEBUF_LEN];
    int fd, ret, count = 0;
    xmlrpc_value *result = NULL;

    /* parse argument array */
    xmlrpc_decompose_value(env, params, "(s)", &path);
    if (env->fault_occurred) {
        return NULL;
    }

    SLOG("va_getfile(), path:%s", path);

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        LOG("open failed: %s", strerror(errno));
        xmlrpc_faultf(env, "open failed: %s", strerror(errno));
        return NULL;
    }

    while ((ret = read(fd, buf, VA_FILEBUF_LEN)) > 0) {
        file_contents = qemu_realloc(file_contents, count + VA_FILEBUF_LEN);
        memcpy(file_contents + count, buf, ret);
        count += ret;
        if (count > VA_GETFILE_MAX) {
            xmlrpc_faultf(env, "max file size (%d bytes) exceeded",
                          VA_GETFILE_MAX);
            goto EXIT_CLOSE_BAD;
        }
    }
    if (ret == -1) {
        LOG("read failed: %s", strerror(errno));
        xmlrpc_faultf(env, "read failed: %s", strerror(errno));
        goto EXIT_CLOSE_BAD;
    }

    result = xmlrpc_build_value(env, "6", file_contents, count);

EXIT_CLOSE_BAD:
    if (file_contents) {
        qemu_free(file_contents);
    }
    close(fd);
    return result;
}
#endif

/* va_getdmesg(): return dmesg output
 * rpc return values:
 *   - dmesg output as a string
 */
#if 0
static xmlrpc_value *va_getdmesg(xmlrpc_env *env,
                              xmlrpc_value *params,
                              void *user_data)
{
    char *dmesg_buf = NULL, cmd[256];
    int ret;
    xmlrpc_value *result = NULL;
    FILE *pipe;

    SLOG("va_getdmesg()");

    dmesg_buf = qemu_mallocz(VA_DMESG_LEN + 2048);
    sprintf(cmd, "dmesg -s %d", VA_DMESG_LEN);

    pipe = popen(cmd, "r");
    if (pipe == NULL) {
        LOG("popen failed: %s", strerror(errno));
        xmlrpc_faultf(env, "popen failed: %s", strerror(errno));
        goto EXIT_NOCLOSE;
    }

    ret = fread(dmesg_buf, sizeof(char), VA_DMESG_LEN, pipe);
    if (!ferror(pipe)) {
        dmesg_buf[ret] = '\0';
        TRACE("dmesg:\n%s", dmesg_buf);
        result = xmlrpc_build_value(env, "s", dmesg_buf);
    } else {
        LOG("fread failed");
        xmlrpc_faultf(env, "popen failed: %s", strerror(errno));
    }

    pclose(pipe);
EXIT_NOCLOSE:
    if (dmesg_buf) {
        qemu_free(dmesg_buf);
    }

    return result;
}

/* va_shutdown(): initiate guest shutdown
 * rpc return values: none
 */
static xmlrpc_value *va_shutdown(xmlrpc_env *env,
                                    xmlrpc_value *params,
                                    void *user_data)
{
    int ret;
    const char *shutdown_type, *shutdown_flag;
    xmlrpc_value *result = xmlrpc_build_value(env, "s", "dummy");

    TRACE("called");
    xmlrpc_decompose_value(env, params, "(s)", &shutdown_type);
    if (env->fault_occurred) {
        goto out_bad;
    }

    if (strcmp(shutdown_type, "halt") == 0) {
        shutdown_flag = "-H";
    } else if (strcmp(shutdown_type, "powerdown") == 0) {
        shutdown_flag = "-P";
    } else if (strcmp(shutdown_type, "reboot") == 0) {
        shutdown_flag = "-r";
    } else {
        xmlrpc_faultf(env, "invalid shutdown type: %s", shutdown_type);
        goto out_bad;
    }

    SLOG("va_shutdown(), shutdown_type:%s", shutdown_type);

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
        TRACE("shouldn't be here");
        exit(0);
    } else if (ret < 0) {
        xmlrpc_faultf(env, "fork() failed: %s", strerror(errno));
    }

    return result;
out_bad:
    return NULL;
}
#endif

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
    TRACE("response: %p", response);
    TRACE("marker");
    QObject *tmp_qobj = QOBJECT(response);
    TRACE("marker");
    QString *tmp_qstr = qobject_to_json(tmp_qobj);
    TRACE("marker");
    const char *tmp_json = qstring_get_str(tmp_qstr);
    TRACE("marker");
    TRACE("formatted rpc json response:\n%s\n", tmp_json);


    return response;
}

/* va_shutdown(): initiate guest shutdown
 * rpc return values: none
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
 * rpc return values: none
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

/* va_ping(): respond to client. response without error in env
 *   variable indicates successful response
 * rpc return values: none
 */
static QDict *va_ping(const QDict *params)
{
    TRACE("called");
    SLOG("va_ping()");
    return va_server_format_response(NULL, 0, NULL);
}

typedef struct RPCFunction {
    QDict *(*func)(const QDict *params);
    const char *func_name;
} RPCFunction;

static QDict *va_capabilities(const QDict *params);

static RPCFunction guest_functions[] = {
    /*
    { .func = va_getfile,
      .func_name = "getfile" },
    { .func = va_getdmesg,
      .func_name = "getdmesg" },
      */
    { .func = va_shutdown,
      .func_name = "shutdown" },
    { .func = va_ping,
      .func_name = "ping" },
    { .func = va_capabilities,
      .func_name = "capabilities" },
    { NULL, NULL }
};

static RPCFunction host_functions[] = {
    { .func = va_ping,
      .func_name = "ping" },
    { .func = va_hello,
      .func_name = "hello" },
    { NULL, NULL }
};

/* va_capabilities(): return server capabilities
 * rpc return values:
 *   - version: virtagent version
 *   - methods: list of supported RPCs
 */
static QDict *va_capabilities(const QDict *params)
{
    QList *functions = qlist_new();
    QDict *ret = qdict_new();
    TRACE("called");
    SLOG("va_capabilities()");
    qlist_append_obj(functions, QOBJECT(qstring_from_str("capabilities")));
    qlist_append_obj(functions, QOBJECT(qstring_from_str("ping")));
    qlist_append_obj(functions, QOBJECT(qstring_from_str("shutdown")));
    qdict_put_obj(ret, "methods", QOBJECT(functions));
    qdict_put_obj(ret, "version", QOBJECT(qstring_from_str(VA_VERSION)));

    return va_server_format_response(ret, 0, NULL);
}

static bool va_server_is_enabled(void)
{
    return va_server_data && va_server_data->enabled;
}

typedef struct VARequestData {
    QDict *request;
    QDict *response;
} VARequestData;

static int va_do_server_rpc(VARequestData *d, const char *tag)
{
    int ret = 0, i;
    const char *func_name;
    RPCFunction *func_list = va_server_data->is_host ?
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

    TRACE("marker");
    if (!va_qdict_haskey_with_type(d->request, "method", QTYPE_QSTRING)) {
        ret = -EINVAL;
        va_server_job_cancel(va_server_data->manager, tag);
        goto out;
    }
    func_name = qdict_get_str(d->request, "method");
    TRACE("marker, specified method: %s", func_name);
    for (i = 0; func_list[i].func != NULL; ++i) {
        TRACE("marker, current method: %s", func_list[i].func_name);
        if (strcmp(func_name, func_list[i].func_name) == 0) {
            TRACE("marker");
            if (va_qdict_haskey_with_type(d->request, "params", QTYPE_QDICT)) {
                params = qdict_get_qdict(d->request, "params");
            }
            response = func_list[i].func(params);
            found = true;
            break;
        }
    }

    if (!response) {
        TRACE("marker");
        if (found) {
            TRACE("marker");
            response = va_server_format_response(NULL, -1,
                                                 "error executing rpc");
        } else {
            TRACE("marker");
            response = va_server_format_response(NULL, -1,
                                                 "unsupported rpc specified");
        }
    }
    TRACE("marker");
    TRACE("called, request data d: %p", d);
    d->response = NULL;
    TRACE("marker");
    d->response = response;
    //d->response = qdict_new();
    TRACE("marker, d->response: %p", d->response);
    qdict_put_obj(d->response, "blah", QOBJECT(qstring_from_str("meh")));
    TRACE("marker");
    QObject *tmp_qobj = QOBJECT(d->response);
    TRACE("marker");
    QString *tmp_qstr = qobject_to_json(tmp_qobj);
    TRACE("marker");
    const char *tmp_json = qstring_get_str(tmp_qstr);
    TRACE("marker");
    TRACE("rpc json response:\n%s\n", tmp_json);

    va_server_job_execute_done(va_server_data->manager, tag);
    /*
    ret = va_server_job_add(resp_xml, tag);
    if (ret != 0) {
        LOG("error adding server job: %s", strerror(ret));
    }
    */

out:
    return ret;
}

/*
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
*/

int va_server_init(VAManager *m, VAServerData *server_data, bool is_host)
{
    //RPCFunction *func_list = is_host ? host_functions : guest_functions;

    va_enable_syslog = !is_host; /* enable logging for guest agent */
    xmlrpc_env_init(&server_data->env);
    server_data->registry = xmlrpc_registry_new(&server_data->env);
    //va_register_functions(&server_data->env, server_data->registry, func_list);
    server_data->enabled = true;
    /* TODO: this is redundant given the is_host arg */
    server_data->is_host = is_host;
    server_data->manager = m;
    va_server_data = server_data;

    return 0;
}

int va_server_close(void)
{
    if (va_server_data != NULL) {
        xmlrpc_registry_free(va_server_data->registry);
        xmlrpc_env_clean(&va_server_data->env);
        va_server_data = NULL;
    }
    return 0;
}

/*
typedef struct VAServerResponse {
    xmlrpc_mem_block *content;
} VAServerResponse;
*/

/* called by VAManager to start executing the RPC */
static int va_execute(void *opaque, const char *tag)
{
    VARequestData *d = opaque;
    //int ret = va_do_server_rpc(d->content, d->content_len, tag);
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
    TRACE("marker");
    QObject *tmp_qobj = QOBJECT(d->response);
    TRACE("marker");
    QString *tmp_qstr = qobject_to_json(tmp_qobj);
    TRACE("marker");
    const char *tmp_json = qstring_get_str(tmp_qstr);
    TRACE("marker");
    //json_resp = qstring_get_str(qobject_to_json(QOBJECT(d->response)));
    json_resp = tmp_json;
    TRACE("marker");
    if (!json_resp) {
        ret = -EINVAL;
        LOG("server generated invalid JSON response");
        goto out_cancel;
    }
    TRACE("marker");
    assert(json_resp);
    TRACE("json_resp:\n%s\n", json_resp);

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
            /* TODO: double-check were handling these refs properly */
            qobject_decref(QOBJECT(d->request));
        }
        if (d->response) {
            /* TODO: double-check were handling these refs properly */
            qobject_decref(QOBJECT(d->response));
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

    TRACE("got content:\n%s\n", content);
    request_obj = qobject_from_json(content);
    if (!request_obj) {
        LOG("unable to parse JSON arguments");
        goto out_bad;
    }
    TRACE("parsed as:\n%s\n", qstring_get_str((qobject_to_json(request_obj))));
    d->request = qobject_to_qdict(request_obj);

    if (!d->request) {
        LOG("recieved qobject of unexpected type: %d", qobject_type(request_obj));
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
