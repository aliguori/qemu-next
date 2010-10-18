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

#include "sysemu.h"
#include "monitor.h"
#include "qjson.h"
#include "qint.h"
#include "cpu-common.h"
#include "kvm.h"
#include "trace.h"
#include "qemu_socket.h"
#include "xmlrpc.h"
#include "xmlrpc_client.h"
#include "virtagent-daemon.h"
#include "virtagent-common.h"
#include "virtagent.h"

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
    int fd = unix_connect(GUEST_AGENT_PATH_CLIENT);
    if (fd < 0) {
        LOG("failed to connect to virtagent service");
    }
    return fd;
}

static int rpc_execute(xmlrpc_env *const env, const char *function,
                       xmlrpc_value *params, xmlrpc_mem_block **resp_xml_p)
{
    xmlrpc_mem_block *call_xml;
    int fd, ret;

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

    ret = va_transport_rpc_call(fd, env, call_xml, resp_xml_p);
    if (ret != 0) {
        ret = -1;
        goto out_callxml;
    }
    TRACE("resp_xml_p = %p\n%s", resp_xml_p,
          XMLRPC_MEMBLOCK_CONTENTS(char, *resp_xml_p));

    ret = 0;
out_callxml:
    XMLRPC_MEMBLOCK_FREE(char, call_xml);
out:
    return ret;
}

/*
 * do_agent_viewfile(): View a text file in the guest
 */
int do_agent_viewfile(Monitor *mon, const QDict *mon_params,
                      QObject **ret_data)
{
    xmlrpc_env env;
    xmlrpc_value *params, *resp = NULL;
    xmlrpc_mem_block *resp_xml;
    const char *filepath;
    char *file_contents = NULL;
    char format[32];
    int ret, file_size;

    filepath = qdict_get_str(mon_params, "filepath");
    xmlrpc_env_init(&env);
    params = xmlrpc_build_value(&env, "(s)", filepath);
    if (rpc_has_error(&env)) {
        ret = -EREMOTE;
        goto out;
    }

    ret = rpc_execute(&env, "getfile", params, &resp_xml);
    if (ret == -EREMOTE) {
        monitor_printf(mon, "RPC Failed (%i): %s\n", env.fault_code,
                       env.fault_string);
        return -1;
    } else if (ret == -1) {
        monitor_printf(mon, "RPC communication error\n");
        return -1;
    }


    resp = xmlrpc_parse_response(&env,
                                 XMLRPC_MEMBLOCK_CONTENTS(char, resp_xml),
                                 XMLRPC_MEMBLOCK_SIZE(char, resp_xml));
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out;
    }

    xmlrpc_parse_value(&env, resp, "6", &file_contents, &file_size);
    if (rpc_has_error(&env)) {
        ret = -1;
        goto out;
    }

    if (file_contents != NULL) {
        /* file text may not be null terminated so explicitly limit
         * output string length to file_size
         */
        sprintf(format, "%%.%ds\n", file_size);
        monitor_printf(mon, format, file_contents);
    }

out:
    XMLRPC_MEMBLOCK_FREE(char, resp_xml);
    xmlrpc_DECREF(resp);

    return ret;
}
