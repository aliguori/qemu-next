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

/* RPC functions common to guest/host daemons */

/* va_getfile(): return file contents
 * rpc return values:
 *   - base64-encoded file contents
 */
static xmlrpc_value *va_getfile(xmlrpc_env *env,
                                xmlrpc_value *param,
                                void *user_data)
{
    const char *path;
    char *file_contents = NULL;
    char buf[VA_FILEBUF_LEN];
    int fd, ret, count = 0;
    xmlrpc_value *result = NULL;

    /* parse argument array */
    xmlrpc_decompose_value(env, param, "(s)", &path);
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
                              xmlrpc_value *param,
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
                                    xmlrpc_value *param,
                                    void *user_data)
{
    int ret;
    const char *shutdown_type, *shutdown_flag;
    xmlrpc_value *result = xmlrpc_build_value(env, "s", "dummy"); 

    TRACE("called");
    xmlrpc_decompose_value(env, param, "(s)", &shutdown_type);
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
                             xmlrpc_value *param,
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
                                   xmlrpc_value *param,
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

static RPCFunction guest_functions[] = {
    { .func = va_getfile,
      .func_name = "va.getfile" },
    { .func = va_getdmesg,
      .func_name = "va.getdmesg" },
    { .func = va_shutdown,
      .func_name = "va.shutdown" },
    { .func = va_ping,
      .func_name = "va.ping" },
    { NULL, NULL }
};
static RPCFunction host_functions[] = {
    { .func = va_ping,
      .func_name = "va.ping" },
    { .func = va_hello,
      .func_name = "va.hello" },
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