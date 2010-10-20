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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <xmlrpc-c/base.h>
#include <xmlrpc-c/server.h>
#include <xmlrpc-c/server_abyss.h>
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

int va_server_loop(int listen_fd, bool is_host)
{
    xmlrpc_registry *registryP;
    xmlrpc_env env;
    int ret, fd;
    char *rpc_request;
    int rpc_request_len;
    xmlrpc_mem_block *rpc_response;
    RPCFunction *func_list = is_host ? host_functions : guest_functions;

    xmlrpc_env_init(&env);
    registryP = xmlrpc_registry_new(&env);
    va_register_functions(&env, registryP, func_list);

    while (1) {
        TRACE("waiting for connection from RPC client");
        fd = va_accept(listen_fd);
        if (fd < 0) {
            break;
        }
        TRACE("RPC client connected, fetching RPC...");
        ret = va_get_rpc_request(fd, &rpc_request, &rpc_request_len);
        if (ret != 0) {
            LOG("error retrieving rpc request");
            goto out;
        }
        TRACE("handling RPC request");
        rpc_response = xmlrpc_registry_process_call(&env, registryP, NULL,
                                                    rpc_request,
                                                    rpc_request_len);
        if (rpc_response == NULL) {
            LOG("error handling rpc request");
            goto out;
        }
        qemu_free(rpc_request);
        ret = va_send_rpc_response(fd, rpc_response);
        if (ret != 0) {
            LOG("error retrieving rpc request");
            goto out;
        }
        qemu_free(rpc_response);
out:
        closesocket(fd);
        xmlrpc_env_clean(&env);
    }

    return 0;
}
