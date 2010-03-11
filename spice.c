#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "qemu-x509.h"
#include "monitor.h"

/* core bits */

static SpiceServer *s;
int using_spice = 0;

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

static void core_log(CoreInterface *core, LogLevel level, const char* componnent,
                     const char* format, ...)
{
    /* ignore */
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

    /* TODO: make configurable via cmdline */
    spice_server_set_image_compression(s, SPICE_IMAGE_COMPRESS_GLZ);

    spice_server_init(s, &core_interface);
    using_spice = 1;

    qemu_spice_input_init(s);

    qemu_free(x509_key_file);
    qemu_free(x509_cert_file);
    qemu_free(x509_cacert_file);
}
