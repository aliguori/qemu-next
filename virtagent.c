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
    /* assume hello is available to we can probe for/notify the host
     * rpc server
     */
    if (strcmp(method, "va.hello") == 0) {
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

int va_client_init(VAClientData *client_data)
{
    client_data->supported_methods = NULL;
    va_client_data = client_data;

    return 0;
}

static int va_rpc_has_error(xmlrpc_env *env)
{
    if (env->fault_occurred) {
        qerror_report(QERR_RPC_FAILED, env->fault_code, env->fault_string);
        return -1;
    }
    return 0;
}

static int va_do_rpc(xmlrpc_env *const env, const char *function,
                     xmlrpc_value *params, VAClientCallback *cb,
                     MonitorCompletion *mon_cb, void *mon_data)
{
    xmlrpc_mem_block *req_xml;
    int ret;

    if (!va_has_capability(function)) {
        LOG("guest agent does not have required capability");
        ret = -1;
        goto out;
    }

    req_xml = XMLRPC_MEMBLOCK_NEW(char, env, 0);
    xmlrpc_serialize_call(env, req_xml, function, params);
    if (va_rpc_has_error(env)) {
        ret = -EINVAL;
        goto out_free;
    }

    ret = va_client_job_add(req_xml, cb, mon_cb, mon_data);
    if (ret) {
        goto out_free;
    }

    return ret;
out_free:
    XMLRPC_MEMBLOCK_FREE(char, req_xml);
out:
    return ret;
}
