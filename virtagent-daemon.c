/*
 * virt-agent - host/guest RPC daemon functions
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
#include "virtagent-daemon.h"
#include "virtagent-common.h"
#include "virtagent.h"

static bool va_enable_syslog = false; /* enable syslog'ing of RPCs */

#define SLOG(msg, ...) do { \
    char msg_buf[1024]; \
    if (!va_enable_syslog) { \
        break; \
    } \
    sprintf(msg_buf, msg, ## __VA_ARGS__); \
    syslog(LOG_INFO, "virtagent, %s", msg_buf); \
} while(0)

/* RPC functions common to guest/host daemons */

static xmlrpc_value *getfile(xmlrpc_env *env,
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

    SLOG("getfile(), path:%s", path);

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

/* getdmesg(): return dmesg output
 * rpc return values:
 *   - dmesg output as a string
 */
static xmlrpc_value *getdmesg(xmlrpc_env *env,
                              xmlrpc_value *param,
                              void *user_data)
{
    char *dmesg_buf = NULL, cmd[256];
    int ret;
    xmlrpc_value *result = NULL;
    FILE *pipe;

    SLOG("getdmesg()");

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
    return result;
}

/* va_hello(): handle client startup notification
 * rpc return values: none
 */

static xmlrpc_value *va_hello(xmlrpc_env *env,
                                   xmlrpc_value *param,
                                   void *user_data)
{
    int ret = va_client_init_capabilities();
    TRACE("called");
    if (ret < 0) {
        LOG("error setting initializing client capabilities");
    }
    return NULL;
}

static int va_accept(int listen_fd) {
    struct sockaddr_in saddr;
    struct sockaddr *addr;
    socklen_t len;
    int fd;

    while (1) {
        len = sizeof(saddr);
        addr = (struct sockaddr *)&saddr;
        fd = qemu_accept(listen_fd, addr, &len);
        if (fd < 0 && errno != EINTR) {
            LOG("accept() failed");
            break;
        } else if (fd >= 0) {
            TRACE("accepted connection");
            break;
        }
    }
    return fd;
}

typedef struct RPCFunction {
    xmlrpc_value *(*func)(xmlrpc_env *env, xmlrpc_value *param, void *unused);
    const char *func_name;
} RPCFunction;

static RPCFunction guest_functions[] = {
    { .func = getfile,
      .func_name = "getfile" },
    { .func = getdmesg,
      .func_name = "getdmesg" },
    { .func = va_shutdown,
      .func_name = "va_shutdown" },
    { .func = va_ping,
      .func_name = "va_ping" },
    { NULL, NULL }
};
static RPCFunction host_functions[] = {
    { .func = va_ping,
      .func_name = "va_ping" },
    { .func = va_hello,
      .func_name = "va_hello" },
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

typedef struct VARPCServerState {
    VPDriver *vp;
    int listen_fd;
    xmlrpc_env env;
    xmlrpc_registry *registry;
} VARPCServerState;

/* only one virtagent server instance can exist at a time */
static VARPCServerState *server_state = NULL;

static void va_accept_handler(void *opaque);

static void va_rpc_send_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    VARPCServerState *s = server_state;

    TRACE("called");
    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error sending RPC response");
    } else {
        TRACE("RPC completed");
    }

    TRACE("waiting for RPC request...");
    vp_set_fd_handler(s->listen_fd, va_accept_handler, NULL, s);
}

static void va_rpc_read_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    VARPCServerState *s = server_state;

    TRACE("called");
    if (rpc_data->status != VA_RPC_STATUS_OK) {
        LOG("error reading RPC request");
        goto out_bad;
    }

    rpc_data->send_resp_xml =
        xmlrpc_registry_process_call(&s->env, s->registry, NULL,
                                     rpc_data->req_xml, rpc_data->req_xml_len);
    if (rpc_data->send_resp_xml == NULL) {
        LOG("error handling RPC request");
        goto out_bad;
    }

    rpc_data->cb = va_rpc_send_cb;
    return;

out_bad:
    TRACE("waiting for RPC request...");
    vp_set_fd_handler(s->listen_fd, va_accept_handler, NULL, s);
}

static void va_accept_handler(void *opaque)
{
    VARPCData *rpc_data;
    int ret, fd;

    TRACE("called");
    fd = va_accept(server_state->listen_fd);
    if (fd < 0) {
        TRACE("connection error: %s", strerror(errno));
        return;
    }
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);

    TRACE("RPC client connected, reading RPC request...");
    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = va_rpc_read_cb;
    ret = va_rpc_read_request(rpc_data, fd);
    if (ret != 0) {
        LOG("error setting up read handler");
        qemu_free(rpc_data);
        return;
    }
    vp_set_fd_handler(server_state->listen_fd, NULL, NULL, NULL);
}

int va_server_init(VPDriver *vp_drv, bool is_host)
{
    RPCFunction *func_list = is_host ? host_functions : guest_functions;
    QemuOpts *opts;
    int ret, fd;
    const char *path, *service_id;

    if (server_state) {
        LOG("virtagent server already initialized");
        return -1;
    }
    va_enable_syslog = !is_host; /* enable logging for guest agent */

    server_state = qemu_mallocz(sizeof(VARPCServerState));
    service_id = is_host ? HOST_AGENT_SERVICE_ID : GUEST_AGENT_SERVICE_ID;
    /* TODO: host agent path needs to be made unique amongst multiple
     * qemu instances
     */
    path = is_host ? HOST_AGENT_PATH : GUEST_AGENT_PATH;

    /* setup listening socket for server */
    opts = qemu_opts_create(qemu_find_opts("net"), "va_server_opts", 0);
    qemu_opt_set(opts, "path", path);
    fd = unix_listen_opts(opts);
    qemu_opts_del(opts);
    if (fd < 0) {
        LOG("error setting up listening socket");
        goto out_bad;
    }

    /* tell virtproxy to forward incoming virtagent connections to the socket */
    ret = vp_set_iforward(vp_drv, service_id, path, NULL, false);
    if (ret < 0) {
        LOG("error setting up virtproxy iforward");
        goto out_bad;
    }

    server_state->vp = vp_drv;
    server_state->listen_fd = fd;
    xmlrpc_env_init(&server_state->env);
    server_state->registry = xmlrpc_registry_new(&server_state->env);
    va_register_functions(&server_state->env, server_state->registry, func_list);

    TRACE("waiting for RPC request...");
    vp_set_fd_handler(server_state->listen_fd, va_accept_handler, NULL,
                      server_state);

    return 0;

out_bad:
    qemu_free(server_state);
    server_state = NULL;
    return -1;
}
