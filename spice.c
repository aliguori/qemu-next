#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
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
            .name = "password",
            .type = QEMU_OPT_STRING,
        },{
            .name = "disable-ticketing",
            .type = QEMU_OPT_BOOL,
        },
        { /* end if list */ }
    },
};

void qemu_spice_init(void)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    const char *password;
    int port;

    if (!opts)
        return;
    port = qemu_opt_get_number(opts, "port", 0);
    if (!port)
        return;
    password = qemu_opt_get(opts, "password");

    s = spice_server_new();
    spice_server_set_port(s, port);
    if (password)
        spice_server_set_ticket(s, password, 0, 0, 0);
    if (qemu_opt_get_bool(opts, "disable-ticketing", 0))
        spice_server_set_noauth(s);

    /* TODO: make configurable via cmdline */
    spice_server_set_image_compression(s, SPICE_IMAGE_COMPRESS_GLZ);

    spice_server_init(s, &core_interface);
    using_spice = 1;

    qemu_spice_input_init(s);
}
