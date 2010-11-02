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
#include "qemu_socket.h"
#include "virtagent-daemon.h"
#include "virtagent-common.h"

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
    char c;
    int pos = 0;
    xmlrpc_value *result = NULL;
    FILE *pipe;

    dmesg_buf = qemu_mallocz(VA_DMESG_LEN + 2048);
    sprintf(cmd, "dmesg -s %d", VA_DMESG_LEN);

    //pipe = popen(cmd, "r");
    pipe = popen("dmesg -s 16000", "r");
    if (pipe == NULL) {
        LOG("popen failed: %s", strerror(errno));
        xmlrpc_faultf(env, "popen failed: %s", strerror(errno));
        goto EXIT_NOCLOSE;
    }

    while ((c = fgetc(pipe)) != EOF && pos < VA_DMESG_LEN) {
        dmesg_buf[pos] = c;
        pos++;
    }
    dmesg_buf[pos++] = '\0';
    TRACE("dmesg:\n%s", dmesg_buf);

    result = xmlrpc_build_value(env, "s", dmesg_buf);
    pclose(pipe);
EXIT_NOCLOSE:
    if (dmesg_buf) {
        qemu_free(dmesg_buf);
    }

    return result;
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
    { NULL, NULL }
};
static RPCFunction host_functions[] = {
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
    int listen_fd;
    xmlrpc_env env;
    xmlrpc_registry *registry;
} VARPCServerState;

static void va_accept_handler(void *opaque);

static void va_rpc_send_cb(void *opaque)
{
    VARPCData *rpc_data = opaque;
    VARPCServerState *s = rpc_data->opaque;

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
    VARPCServerState *s = rpc_data->opaque;

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
    VARPCServerState *s = opaque;
    VARPCData *rpc_data;
    int ret, fd;

    TRACE("called");
    fd = va_accept(s->listen_fd);
    if (fd < 0) {
        TRACE("connection error: %s", strerror(errno));
        return;
    }
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);

    TRACE("RPC client connected, reading RPC request...");
    rpc_data = qemu_mallocz(sizeof(VARPCData));
    rpc_data->cb = va_rpc_read_cb;
    rpc_data->opaque = s;
    ret = va_rpc_read_request(rpc_data, fd);
    if (ret != 0) {
        LOG("error setting up read handler");
        qemu_free(rpc_data);
        return;
    }
    vp_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
}

int va_server_start(int listen_fd, bool is_host)
{
    VARPCServerState *s;
    RPCFunction *func_list = is_host ? host_functions : guest_functions;

    s = qemu_mallocz(sizeof(VARPCServerState));
    s->listen_fd = listen_fd;
    xmlrpc_env_init(&s->env);
    s->registry = xmlrpc_registry_new(&s->env);
    va_register_functions(&s->env, s->registry, func_list);

    TRACE("waiting for RPC request...");
    vp_set_fd_handler(s->listen_fd, va_accept_handler, NULL, s);

    return 0;
}
