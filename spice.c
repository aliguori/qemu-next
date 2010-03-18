#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu_socket.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "qemu-x509.h"
#include "monitor.h"
#include "qerror.h"
#include "qjson.h"
#include "sysemu.h"
#include "vnc.h"

/* core bits */

static SpiceServer *s;
int using_spice = 0;

void qemu_spice_migrate_start(void)
{
    if (!s)
        return;
    spice_server_migrate_start(s);
}

void qemu_spice_migrate_end(int completed)
{
    if (!s)
        return;
    spice_server_migrate_end(s, completed);
}

void qemu_spice_add_interface(VDInterface *interface)
{
    if (!s)
        return;
    spice_server_add_interface(s, interface);
}

void qemu_spice_remove_interface(VDInterface *interface)
{
    if (!s)
        return;
    spice_server_remove_interface(s, interface);
}

static VDObjectRef core_create_timer(CoreInterface *core, timer_callback_t callback, void *opaue)
{
    return (VDObjectRef)qemu_new_timer(rt_clock, callback, opaue);
}

static void core_arm_timer(CoreInterface *core, VDObjectRef timer, uint32_t ms)
{
    qemu_mod_timer((QEMUTimer *)timer, qemu_get_clock(rt_clock) + ms);
}

static void core_disarm_timer(CoreInterface *core, VDObjectRef timer)
{
    qemu_del_timer((QEMUTimer *)timer);
}

static void core_destroy_timer(CoreInterface *core, VDObjectRef timer)
{
    qemu_del_timer((QEMUTimer *)timer);
    qemu_free_timer((QEMUTimer *)timer);
}

static int core_set_file_handlers(CoreInterface *core, int fd,
                              void (*on_read)(void *),
                              void (*on_write)(void *),
                              void *opaque)
{
    return qemu_set_fd_handler(fd, on_read, on_write, opaque);
}

static void core_term_printf(CoreInterface *core, const char* format, ...)
{
    /* ignore */
}

static QDict *server, *client;

static void spice_qmp_event_initialized(void)
{
    struct sockaddr_storage sa;
    char addr[NI_MAXHOST], port[NI_MAXSERV];
    socklen_t salen;
    QObject *data;

    QDECREF(server);
    server = qdict_new();
    salen = sizeof(sa);
    if (spice_server_get_sock_info(s, (struct sockaddr*)&sa, &salen) == 0) {
        if (getnameinfo((struct sockaddr*)&sa, salen,
                        addr, sizeof(addr), port, sizeof(port),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            qdict_put(server, "host", qstring_from_str(addr));
            qdict_put(server, "family", qstring_from_str(inet_strfamily(sa.ss_family)));
        }
    }

    QDECREF(client);
    client = qdict_new();
    salen = sizeof(sa);
    if (spice_server_get_peer_info(s, (struct sockaddr*)&sa, &salen) == 0) {
        if (getnameinfo((struct sockaddr*)&sa, salen,
                        addr, sizeof(addr), port, sizeof(port),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            qdict_put(client, "host", qstring_from_str(addr));
            qdict_put(client, "family", qstring_from_str(inet_strfamily(sa.ss_family)));
        }
    }

    data = qobject_from_jsonf("{ 'client': %p, 'server': %p }",
                              QOBJECT(client), QOBJECT(server));
    monitor_protocol_event(QEVENT_SPICE_INITIALIZED, data);
    QINCREF(client);
    QINCREF(server);
    qobject_decref(data);
}

static void spice_qmp_event_disconnect(void)
{
    QObject *data;

    /*
     * Right now spice does (a) support one connection at a time only
     * and (b) allways sends disconnects for the old client before the
     * connect for new client.  So we can simply reuse the server and
     * client info collected on connect for the time being.
     */
    data = qobject_from_jsonf("{ 'client': %p, 'server': %p }",
                              QOBJECT(client), QOBJECT(server));
    monitor_protocol_event(QEVENT_SPICE_DISCONNECTED, data);
    qobject_decref(data);
    server = NULL;
    client = NULL;
}

static void core_log(CoreInterface *core, LogLevel level, const char* componnent,
                     const char* format, ...)
{
    if (strcmp(format, "new user connection") == 0) {
        spice_qmp_event_initialized();
    }
    if (strcmp(format, "user disconnected") == 0) {
        spice_qmp_event_disconnect();
    }
}

static CoreInterface core_interface = {
    .base.base_version  = VM_INTERFACE_VERSION,
    .base.type          = VD_INTERFACE_CORE,
    .base.description   = "qemu core services",
    .base.major_version = VD_INTERFACE_CORE_MAJOR,
    .base.minor_version = VD_INTERFACE_CORE_MINOR,
    .create_timer       = core_create_timer,
    .arm_timer          = core_arm_timer,
    .disarm_timer       = core_disarm_timer,
    .destroy_timer      = core_destroy_timer,
    .set_file_handlers  = core_set_file_handlers,
    .term_printf        = core_term_printf,
    .log                = core_log,
};

static int name2enum(const char *name, const char *table[], int entries)
{
    int i;

    if (name) {
        for (i = 0; i < entries; i++) {
            if (!table[i])
                continue;
            if (strcmp(name, table[i]) != 0)
                continue;
            return i;
        }
    }
    return -1;
}

static const char *compression_names[] = {
    [ SPICE_IMAGE_COMPRESS_OFF ]      = "off",
    [ SPICE_IMAGE_COMPRESS_AUTO_GLZ ] = "auto_glz",
    [ SPICE_IMAGE_COMPRESS_AUTO_LZ ]  = "auto_lz",
    [ SPICE_IMAGE_COMPRESS_QUIC ]     = "quic",
    [ SPICE_IMAGE_COMPRESS_GLZ ]      = "glz",
    [ SPICE_IMAGE_COMPRESS_LZ ]       = "lz",
};
#define parse_compression(_name) \
    name2enum(_name, compression_names, ARRAY_SIZE(compression_names))

static const char *channel_names[] = {
    [ SPICE_CHANNEL_MAIN ]      = "main",
    [ SPICE_CHANNEL_DISPLAY ]   = "display",
    [ SPICE_CHANNEL_INPUTS ]    = "inputs",
    [ SPICE_CHANNEL_CURSOR ]    = "cursor",
    [ SPICE_CHANNEL_PLAYBACK ]  = "playback",
    [ SPICE_CHANNEL_RECORD ]    = "record",
    [ SPICE_CHANNEL_TUNNEL ]    = "tunnel",
    [ SPICE_CHANNEL_ALL ]       = "all",
};
#define parse_channel(_name) \
    name2enum(_name, channel_names, ARRAY_SIZE(channel_names))

/* functions for the rest of qemu */

QemuOptsList qemu_spice_opts = {
    .name = "spice",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_spice_opts.head),
    .desc = {
        {
            .name = "port",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "tls-port",           /* old: sport */
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "addr",               /* old: host */
            .type = QEMU_OPT_STRING,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "password",
            .type = QEMU_OPT_STRING,
        },{
            .name = "disable-ticketing",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "image-compression",  /* old: ic */
            .type = QEMU_OPT_STRING,
        },{
            .name = "renderer",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "plaintext-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-dir",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-file",      /* old: sslkey */
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-password",  /* old: sslpassword */
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cert-file",     /* old: sslcert */
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cacert-file",   /* old: sslcafile */
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-dh-key-file",   /* old: ssldhfile */
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-ciphers",        /* old: sslciphersuite */
            .type = QEMU_OPT_STRING,
        },
        { /* end if list */ }
    },
};

void mon_set_password(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *password  = qdict_get_str(qdict, "password");
    const char *connected = qdict_get_try_str(qdict, "connected");
    int lifetime          = qdict_get_int(qdict, "expiration");
    int disconnect_if_connected = 0;
    int fail_if_connected = 0;
    int rc;

    if (connected) {
        if (strcmp(connected, "fail") == 0) {
            fail_if_connected = 1;
        } else if (strcmp(connected, "disconnect") == 0) {
            disconnect_if_connected = 1;
        } else if (strcmp(connected, "keep") == 0) {
            /* nothing */
        } else {
            qemu_error_new(QERR_INVALID_PARAMETER, "connected");
            return;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!s) {
            /* correct one? spice isn't a device ,,, */
            qemu_error_new(QERR_DEVICE_NOT_ACTIVE, "spice");
            return;
        }
        rc = spice_server_set_ticket(s, password, lifetime,
                                     fail_if_connected,
                                     disconnect_if_connected);
        if (rc != 0) {
            qemu_error_new(QERR_SET_PASSWD_FAILED);
            return;
        }

    } else if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            qemu_error_new(QERR_INVALID_PARAMETER, "connected");
            return;
        }
        if (vnc_display_password(NULL, password, lifetime) < 0)
            qemu_error_new(QERR_SET_PASSWD_FAILED);

    } else {
        qemu_error_new(QERR_INVALID_PARAMETER, "protocol");
    }
}

static int add_renderer(const char *name, const char *value, void *opaque)
{
    if (strcmp(name, "renderer") != 0)
        return 0;
    spice_server_add_renderer(s, value);
    return 0;
}

static int add_channel(const char *name, const char *value, void *opaque)
{
    spice_channel_t channel;
    int security = 0;

    if (strcmp(name, "tls-channel") == 0)
        security = SPICE_CHANNEL_SECURITY_SSL;
    if (strcmp(name, "plaintext-channel") == 0)
        security = SPICE_CHANNEL_SECURITY_NON;
    if (security == 0)
        return 0;
    channel = parse_channel(value);
    if (channel == -1) {
        fprintf(stderr, "spice: failed to parse channel: %s\n", value);
        exit(1);
    }
    spice_server_set_channel_security(s, channel, security);
    return 0;
}

void qemu_spice_init(void)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    const char *password, *str, *addr, *x509_dir,
        *x509_key_password = NULL,
        *x509_dh_file = NULL,
        *tls_ciphers = NULL;
    char *x509_key_file = NULL,
        *x509_cert_file = NULL,
        *x509_cacert_file = NULL;
    int port, tls_port, len, addr_flags;
    spice_image_compression_t compression;

    if (!opts)
        return;
    port = qemu_opt_get_number(opts, "port", 0);
    tls_port = qemu_opt_get_number(opts, "tls-port", 0);
    if (!port && !tls_port)
        return;
    password = qemu_opt_get(opts, "password");

    if (tls_port) {
        x509_dir = qemu_opt_get(opts, "x509-dir");
        if (NULL == x509_dir)
            x509_dir = ".";
        len = strlen(x509_dir) + 32;

        str = qemu_opt_get(opts, "x509-key-file");
        if (str) {
            x509_key_file = qemu_strdup(str);
        } else {
            x509_key_file = qemu_malloc(len);
            snprintf(x509_key_file, len, "%s/%s", x509_dir, X509_SERVER_KEY_FILE);
        }

        str = qemu_opt_get(opts, "x509-cert-file");
        if (str) {
            x509_cert_file = qemu_strdup(str);
        } else {
            x509_cert_file = qemu_malloc(len);
            snprintf(x509_cert_file, len, "%s/%s", x509_dir, X509_SERVER_CERT_FILE);
        }

        str = qemu_opt_get(opts, "x509-cacert-file");
        if (str) {
            x509_cacert_file = qemu_strdup(str);
        } else {
            x509_cacert_file = qemu_malloc(len);
            snprintf(x509_cacert_file, len, "%s/%s", x509_dir, X509_CA_CERT_FILE);
        }

        x509_key_password = qemu_opt_get(opts, "x509-key-password");
        x509_dh_file = qemu_opt_get(opts, "x509-dh-file");
        tls_ciphers = qemu_opt_get(opts, "tls-ciphers");
    }

    str = qemu_opt_get(opts, "image-compression");
    if (str) {
        compression = parse_compression(str);
        if (compression == -1) {
            fprintf(stderr, "spice: invalid image compression: %s\n", str);
            exit(1);
        }
    } else {
        compression = SPICE_IMAGE_COMPRESS_AUTO_GLZ;
    }

    addr = qemu_opt_get(opts, "addr");
    addr_flags = 0;
    if (qemu_opt_get_bool(opts, "ipv4", 0))
        addr_flags |= SPICE_ADDR_FLAG_IPV4_ONLY;
    else if (qemu_opt_get_bool(opts, "ipv6", 0))
        addr_flags |= SPICE_ADDR_FLAG_IPV6_ONLY;

    s = spice_server_new();
    spice_server_set_addr(s, addr ? addr : "", addr_flags);
    if (port) {
        spice_server_set_port(s, port);
    }
    if (tls_port) {
        spice_server_set_tls(s, tls_port,
                             x509_cacert_file,
                             x509_cert_file,
                             x509_key_file,
                             x509_key_password,
                             x509_dh_file,
                             tls_ciphers);
    }
    if (password)
        spice_server_set_ticket(s, password, 0, 0, 0);
    if (qemu_opt_get_bool(opts, "disable-ticketing", 0))
        spice_server_set_noauth(s);

    spice_server_set_image_compression(s, compression);
    qemu_opt_foreach(opts, add_channel, NULL, 0);
    qemu_opt_foreach(opts, add_renderer, NULL, 0);

    spice_server_init(s, &core_interface);
    using_spice = 1;

    qemu_spice_input_init(s);

    qemu_free(x509_key_file);
    qemu_free(x509_cert_file);
    qemu_free(x509_cacert_file);
}
