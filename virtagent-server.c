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

/* va_getdmesg(): return dmesg output
 * rpc return values:
 *   - dmesg output as a string
 */
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

/* va_ping(): respond to client. response without error in env
 *   variable indicates successful response
 * rpc return values: none
 */
static xmlrpc_value *va_ping(xmlrpc_env *env,
                             xmlrpc_value *params,
                             void *user_data)
{
    xmlrpc_value *result = xmlrpc_build_value(env, "s", "dummy");
    SLOG("va_ping()");
    return result;
}

/* va_hello(): handle client startup notification
 * rpc return values: none
 */

static xmlrpc_value *va_hello(xmlrpc_env *env,
                                   xmlrpc_value *params,
                                   void *user_data)
{
    xmlrpc_value *result;
    int ret;
    TRACE("called");
    SLOG("va_hello()");
    result = xmlrpc_build_value(env, "s", "dummy");
    ret = va_client_init_capabilities();
    if (ret < 0) {
        LOG("error setting initializing client capabilities");
    }
    return result;
}

typedef struct RPCFunction {
    xmlrpc_value *(*func)(xmlrpc_env *env, xmlrpc_value *param, void *unused);
    const char *func_name;
} RPCFunction;

static xmlrpc_value *va_capabilities(xmlrpc_env *env,
                                     xmlrpc_value *params,
                                     void *user_data);

static RPCFunction guest_functions[] = {
    { .func = va_getfile,
      .func_name = "va.getfile" },
    { .func = va_getdmesg,
      .func_name = "va.getdmesg" },
    { .func = va_shutdown,
      .func_name = "va.shutdown" },
    { .func = va_ping,
      .func_name = "va.ping" },
    { .func = va_capabilities,
      .func_name = "va.capabilities" },
    { NULL, NULL }
};
static RPCFunction host_functions[] = {
    { .func = va_ping,
      .func_name = "va.ping" },
    { .func = va_hello,
      .func_name = "va.hello" },
    { .func = va_capabilities,
      .func_name = "va.capabilities" },
    { NULL, NULL }
};

/* va_capabilities(): return server capabilities
 * rpc return values:
 *   - version: virtagent version
 *   - methods: list of supported RPCs
 */
static xmlrpc_value *va_capabilities(xmlrpc_env *env,
                                     xmlrpc_value *params,
                                     void *user_data)
{
    int i;
    xmlrpc_value *result = NULL, *methods;
    RPCFunction *func_list = va_server_data->is_host ?
                             host_functions : guest_functions;

    /* provide a list of supported RPCs. we don't want to rely on
     * system.methodList since introspection methods won't support
     * client metadata, which we may eventually come to rely upon
     */
    methods = xmlrpc_array_new(env);
    for (i = 0; func_list[i].func != NULL; ++i) {
        xmlrpc_array_append_item(env, methods,
                                 xmlrpc_string_new(env, func_list[i].func_name));
    }

    result = xmlrpc_build_value(env, "{s:s,s:A}", "version", VA_VERSION,
                                "methods", methods);
    return result;
}

static bool va_server_is_enabled(void)
{
    return va_server_data && va_server_data->enabled;
}

typedef struct VARequestData {
    void *content;
    size_t content_len;
    xmlrpc_mem_block *resp_xml;
} VARequestData;

static int va_do_server_rpc(VARequestData *d, const char *tag)
//int va_do_server_rpc(const char *content, size_t content_len, const char *tag)
{
    int ret = 0;

    TRACE("called");

    if (!va_server_is_enabled()) {
        ret = -EBUSY;
        goto out;
    }
    d->resp_xml = xmlrpc_registry_process_call(&va_server_data->env,
                                            va_server_data->registry,
                                            NULL, d->content, d->content_len);
    if (d->resp_xml == NULL) {
        LOG("error processing RPC request");
        ret = -EINVAL;
        goto out;
    }

    /* TODO: we need a way to pass resp back */

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

int va_server_init(VAManager *m, VAServerData *server_data, bool is_host)
{
    RPCFunction *func_list = is_host ? host_functions : guest_functions;

    va_enable_syslog = !is_host; /* enable logging for guest agent */
    xmlrpc_env_init(&server_data->env);
    server_data->registry = xmlrpc_registry_new(&server_data->env);
    va_register_functions(&server_data->env, server_data->registry, func_list);
    server_data->enabled = true;
    server_data->is_host = true;
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
    if (ret) {
        LOG("error occurred executing RPC: %s", strerror(ret));
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
    int ret = va_xport_send_response(XMLRPC_MEMBLOCK_CONTENTS(char, d->resp_xml),
                                     XMLRPC_MEMBLOCK_SIZE(char, d->resp_xml),
                                     tag, tag, va_send_response_cb);
    return ret;
}

static int va_cleanup(void *opaque, const char *tag)
{
    VARequestData *d = opaque;
    if (d) {
        if (d->content) {
            qemu_free(d->content);
        }
        if (d->resp_xml) {
            XMLRPC_MEMBLOCK_FREE(char, d->resp_xml);
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
int va_server_job_create(char *content, size_t content_len, const char *tag)
{
    VARequestData *d = qemu_mallocz(sizeof(VAServerData));
    d->content = content;
    d->content_len = content_len;
    va_server_job_add(va_server_data->manager, tag, d, server_job_ops);
    return 0;
}
