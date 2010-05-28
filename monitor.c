/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <dirent.h>
#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "hw/watchdog.h"
#include "hw/loader.h"
#include "gdbstub.h"
#include "net.h"
#include "net/slirp.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "monitor.h"
#include "readline.h"
#include "console.h"
#include "block.h"
#include "audio/audio.h"
#include "disas.h"
#include "balloon.h"
#include "qemu-timer.h"
#include "migration.h"
#include "kvm.h"
#include "acl.h"
#include "qint.h"
#include "qfloat.h"
#include "qlist.h"
#include "qbool.h"
#include "qstring.h"
#include "qjson.h"
#include "json-streamer.h"
#include "json-parser.h"
#include "osdep.h"

//#define DEBUG
//#define DEBUG_COMPLETION

/*
 * Supported types:
 *
 * 'F'          filename
 * 'B'          block device name
 * 's'          string (accept optional quote)
 * 'O'          option string of the form NAME=VALUE,...
 *              parsed according to QemuOptsList given by its name
 *              Example: 'device:O' uses qemu_device_opts.
 *              Restriction: only lists with empty desc are supported
 *              TODO lift the restriction
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * 'M'          just like 'l', except in user mode the value is
 *              multiplied by 2^20 (think Mebibyte)
 * 'f'          double
 *              user mode accepts an optional G, g, M, m, K, k suffix,
 *              which multiplies the value by 2^30 for suffixes G and
 *              g, 2^20 for M and m, 2^10 for K and k
 * 'T'          double
 *              user mode accepts an optional ms, us, ns suffix,
 *              which divides the value by 1e3, 1e6, 1e9, respectively
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for all types, except '/')
 * '.'          other form of optional type (for 'i' and 'l')
 * 'b'          boolean
 *              user mode accepts "on" or "off"
 * '-'          optional parameter (eg. '-f')
 *
 */

typedef struct MonitorCompletionData MonitorCompletionData;
struct MonitorCompletionData {
    Monitor *mon;
    QString *id;
    void (*user_print)(Monitor *mon, const QObject *data);
};

typedef struct mon_cmd_t {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    void (*user_print)(Monitor *mon, const QObject *data);
    union {
        void (*info)(Monitor *mon);
        void (*info_new)(Monitor *mon, QObject **ret_data);
        int  (*info_async)(Monitor *mon, MonitorCompletion *cb, void *opaque);
        void (*cmd)(Monitor *mon, const QDict *qdict);
        int  (*cmd_new)(Monitor *mon, const QDict *params, QObject **ret_data);
        int  (*cmd_async)(Monitor *mon, const QDict *params,
                          MonitorCompletion *cb, void *opaque);
    } mhandler;
    int async;
} mon_cmd_t;

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

typedef struct MonitorControl {
    JSONMessageParser parser;
    int command_mode;
} MonitorControl;

struct Monitor {
    CharDriverState *chr;
    int mux_out;
    int reset_seen;
    int flags;
    int suspend_cnt;
    uint8_t outbuf[1024];
    int outbuf_index;
    ReadLineState *rs;
    MonitorControl *mc;
    CPUState *mon_cpu;
    BlockDriverCompletionFunc *password_completion_cb;
    void *password_opaque;
#ifdef CONFIG_DEBUG_MONITOR
    int print_calls_nr;
#endif
    QError *error;
    QLIST_HEAD(,mon_fd_t) fds;
    QLIST_ENTRY(Monitor) entry;
};

#ifdef CONFIG_DEBUG_MONITOR
#define MON_DEBUG(fmt, ...) do {    \
    fprintf(stderr, "Monitor: ");       \
    fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)

static inline void mon_print_count_inc(Monitor *mon)
{
    mon->print_calls_nr++;
}

static inline void mon_print_count_init(Monitor *mon)
{
    mon->print_calls_nr = 0;
}

static inline int mon_print_count_get(const Monitor *mon)
{
    return mon->print_calls_nr;
}

#else /* !CONFIG_DEBUG_MONITOR */
#define MON_DEBUG(fmt, ...) do { } while (0)
static inline void mon_print_count_inc(Monitor *mon) { }
static inline void mon_print_count_init(Monitor *mon) { }
static inline int mon_print_count_get(const Monitor *mon) { return 0; }
#endif /* CONFIG_DEBUG_MONITOR */

static QLIST_HEAD(mon_list, Monitor) mon_list;

static const mon_cmd_t mon_cmds[];
static const mon_cmd_t info_cmds[];

Monitor *cur_mon;
Monitor *default_mon;

static void monitor_command_cb(Monitor *mon, const char *cmdline,
                               void *opaque);

static inline int qmp_cmd_mode(const Monitor *mon)
{
    return (mon->mc ? mon->mc->command_mode : 0);
}

/* Return true if in control mode, false otherwise */
static inline int monitor_ctrl_mode(const Monitor *mon)
{
    return (mon->flags & MONITOR_USE_CONTROL);
}

/* Return non-zero iff we have a current monitor, and it is in QMP mode.  */
int monitor_cur_is_qmp(void)
{
    return cur_mon && monitor_ctrl_mode(cur_mon);
}

static void monitor_read_command(Monitor *mon, int show_prompt)
{
    if (!mon->rs)
        return;

    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt)
        readline_show_prompt(mon->rs);
}

static int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
                                 void *opaque)
{
    if (monitor_ctrl_mode(mon)) {
        qerror_report(QERR_MISSING_PARAMETER, "password");
        return -EINVAL;
    } else if (mon->rs) {
        readline_start(mon->rs, "Password: ", 1, readline_func, opaque);
        /* prompt is printed on return from the command handler */
        return 0;
    } else {
        monitor_printf(mon, "terminal does not support password prompting\n");
        return -ENOTTY;
    }
}

void monitor_flush(Monitor *mon)
{
    if (mon && mon->outbuf_index != 0 && !mon->mux_out) {
        qemu_chr_write(mon->chr, mon->outbuf, mon->outbuf_index);
        mon->outbuf_index = 0;
    }
}

/* flush at every end of line or if the buffer is full */
static void monitor_puts(Monitor *mon, const char *str)
{
    char c;

    for(;;) {
        c = *str++;
        if (c == '\0')
            break;
        if (c == '\n')
            mon->outbuf[mon->outbuf_index++] = '\r';
        mon->outbuf[mon->outbuf_index++] = c;
        if (mon->outbuf_index >= (sizeof(mon->outbuf) - 1)
            || c == '\n')
            monitor_flush(mon);
    }
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    char buf[4096];

    if (!mon)
        return;

    mon_print_count_inc(mon);

    if (monitor_ctrl_mode(mon)) {
        return;
    }

    vsnprintf(buf, sizeof(buf), fmt, ap);
    monitor_puts(mon, buf);
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    va_end(ap);
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
    int i;

    for (i = 0; filename[i]; i++) {
        switch (filename[i]) {
        case ' ':
        case '"':
        case '\\':
            monitor_printf(mon, "\\%c", filename[i]);
            break;
        case '\t':
            monitor_printf(mon, "\\t");
            break;
        case '\r':
            monitor_printf(mon, "\\r");
            break;
        case '\n':
            monitor_printf(mon, "\\n");
            break;
        default:
            monitor_printf(mon, "%c", filename[i]);
            break;
        }
    }
}

static int monitor_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf((Monitor *)stream, fmt, ap);
    va_end(ap);
    return 0;
}

static void monitor_user_noop(Monitor *mon, const QObject *data) { }

static inline int monitor_handler_ported(const mon_cmd_t *cmd)
{
    return cmd->user_print != NULL;
}

static inline bool monitor_handler_is_async(const mon_cmd_t *cmd)
{
    return cmd->async != 0;
}

static inline int monitor_has_error(const Monitor *mon)
{
    return mon->error != NULL;
}

static void monitor_json_emitter(Monitor *mon, const QObject *data)
{
    QString *json;

    json = qobject_to_json(data);
    assert(json != NULL);

    qstring_append_chr(json, '\n');
    monitor_puts(mon, qstring_get_str(json));

    QDECREF(json);
}

static void monitor_protocol_emitter(Monitor *mon, QObject *data, QString *id)
{
    QDict *qmp;

    qmp = qdict_new();

    if (!monitor_has_error(mon)) {
        /* success response */
        if (data) {
            qobject_incref(data);
            qdict_put_obj(qmp, "return", data);
        } else {
            /* return an empty QDict by default */
            qdict_put(qmp, "return", qdict_new());
        }
    } else {
        /* error response */
        qdict_put(mon->error->error, "desc", qerror_human(mon->error));
        qdict_put(qmp, "error", mon->error->error);
        QINCREF(mon->error->error);
        QDECREF(mon->error);
        mon->error = NULL;
    }

    if (id) {
        QINCREF(id);
        qdict_put_obj(qmp, "id", QOBJECT(id));
    }

    monitor_json_emitter(mon, QOBJECT(qmp));
    QDECREF(qmp);
}

static void timestamp_put(QDict *qdict)
{
    int err;
    QObject *obj;
    qemu_timeval tv;

    err = qemu_gettimeofday(&tv);
    if (err < 0)
        return;

    obj = qobject_from_jsonf("{ 'seconds': %" PRId64 ", "
                                "'microseconds': %" PRId64 " }",
                                (int64_t) tv.tv_sec, (int64_t) tv.tv_usec);
    qdict_put_obj(qdict, "timestamp", obj);
}

/**
 * monitor_protocol_event(): Generate a Monitor event
 *
 * Event-specific data can be emitted through the (optional) 'data' parameter.
 */
void monitor_protocol_event(MonitorEvent event, QObject *data)
{
    QDict *qmp;
    const char *event_name;
    Monitor *mon;

    assert(event < QEVENT_MAX);

    switch (event) {
        case QEVENT_SHUTDOWN:
            event_name = "SHUTDOWN";
            break;
        case QEVENT_RESET:
            event_name = "RESET";
            break;
        case QEVENT_POWERDOWN:
            event_name = "POWERDOWN";
            break;
        case QEVENT_STOP:
            event_name = "STOP";
            break;
        case QEVENT_RESUME:
            event_name = "RESUME";
            break;
        case QEVENT_VNC_CONNECTED:
            event_name = "VNC_CONNECTED";
            break;
        case QEVENT_VNC_INITIALIZED:
            event_name = "VNC_INITIALIZED";
            break;
        case QEVENT_VNC_DISCONNECTED:
            event_name = "VNC_DISCONNECTED";
            break;
        case QEVENT_BLOCK_IO_ERROR:
            event_name = "BLOCK_IO_ERROR";
            break;
        case QEVENT_RTC_CHANGE:
            event_name = "RTC_CHANGE";
            break;
        case QEVENT_WATCHDOG:
            event_name = "WATCHDOG";
            break;
        default:
            abort();
            break;
    }

    qmp = qdict_new();
    timestamp_put(qmp);
    qdict_put(qmp, "event", qstring_from_str(event_name));
    if (data) {
        qobject_incref(data);
        qdict_put_obj(qmp, "data", data);
    }

    QLIST_FOREACH(mon, &mon_list, entry) {
        if (monitor_ctrl_mode(mon) && qmp_cmd_mode(mon)) {
            monitor_json_emitter(mon, QOBJECT(qmp));
        }
    }
    QDECREF(qmp);
}

static int do_qmp_capabilities(Monitor *mon, const QDict *params,
                               QObject **ret_data)
{
    /* Will setup QMP capabilities in the future */
    if (monitor_ctrl_mode(mon)) {
        mon->mc->command_mode = 1;
    }

    return 0;
}

static int compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        if ((p - pstart) == len && !memcmp(pstart, name, len))
            return 1;
        if (*p == '\0')
            break;
        p++;
    }
    return 0;
}

static void help_cmd_dump(Monitor *mon, const mon_cmd_t *cmds,
                          const char *prefix, const char *name)
{
    const mon_cmd_t *cmd;

    for(cmd = cmds; cmd->name != NULL; cmd++) {
        if (!name || !strcmp(name, cmd->name))
            monitor_printf(mon, "%s%s %s -- %s\n", prefix, cmd->name,
                           cmd->params, cmd->help);
    }
}

static void help_cmd(Monitor *mon, const char *name)
{
    if (name && !strcmp(name, "info")) {
        help_cmd_dump(mon, info_cmds, "info ", NULL);
    } else {
        help_cmd_dump(mon, mon_cmds, "", name);
        if (name && !strcmp(name, "log")) {
            const CPULogItem *item;
            monitor_printf(mon, "Log items (comma separated):\n");
            monitor_printf(mon, "%-10s %s\n", "none", "remove all logs");
            for(item = cpu_log_items; item->mask != 0; item++) {
                monitor_printf(mon, "%-10s %s\n", item->name, item->help);
            }
        }
    }
}

static void do_help_cmd(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

static void user_monitor_complete(void *opaque, QObject *ret_data)
{
    MonitorCompletionData *data = (MonitorCompletionData *)opaque; 

    if (ret_data) {
        if (qobject_type(ret_data) == QTYPE_QERROR) {
            QError *err = qobject_to_qerror(ret_data);
            QString *msg = qerror_human(err);

            monitor_printf(data->mon, "%s\n", qstring_get_str(msg));
            QDECREF(msg);
        } else {
            data->user_print(data->mon, ret_data);
        }
    }
    monitor_resume(data->mon);
    qemu_free(data);
}

static void qmp_monitor_complete(void *opaque, QObject *ret_data)
{
    MonitorCompletionData *data = (MonitorCompletionData *)opaque;

    printf("ret_data %p\n", ret_data);
    printf("data->id %p\n", data->id);

    if (ret_data && qobject_type(ret_data) == QTYPE_QERROR) {
        monitor_set_error(data->mon, qobject_to_qerror(ret_data));
        ret_data = NULL;
    }
    monitor_protocol_emitter(opaque, ret_data, data->id);
}

static void qmp_async_cmd_handler(Monitor *mon, const mon_cmd_t *cmd,
                                  const QDict *params, QString *id)
{
    MonitorCompletionData *data = qemu_mallocz(sizeof(*data));

    data->mon = mon;
    data->id = id;
    cmd->mhandler.cmd_async(mon, params, qmp_monitor_complete, data);
}

static void qmp_async_info_handler(Monitor *mon, const mon_cmd_t *cmd)
{
    cmd->mhandler.info_async(mon, qmp_monitor_complete, mon);
}

static void user_async_cmd_handler(Monitor *mon, const mon_cmd_t *cmd,
                                   const QDict *params)
{
    int ret;

    MonitorCompletionData *cb_data = qemu_malloc(sizeof(*cb_data));
    cb_data->mon = mon;
    cb_data->user_print = cmd->user_print;
    monitor_suspend(mon);
    ret = cmd->mhandler.cmd_async(mon, params,
                                  user_monitor_complete, cb_data);
    if (ret < 0) {
        monitor_resume(mon);
        qemu_free(cb_data);
    }
}

static void user_async_info_handler(Monitor *mon, const mon_cmd_t *cmd)
{
    int ret;

    MonitorCompletionData *cb_data = qemu_malloc(sizeof(*cb_data));
    cb_data->mon = mon;
    cb_data->user_print = cmd->user_print;
    monitor_suspend(mon);
    ret = cmd->mhandler.info_async(mon, user_monitor_complete, cb_data);
    if (ret < 0) {
        monitor_resume(mon);
        qemu_free(cb_data);
    }
}

static int do_info(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const mon_cmd_t *cmd;
    const char *item = qdict_get_try_str(qdict, "item");

    if (!item) {
        assert(monitor_ctrl_mode(mon) == 0);
        goto help;
    }

    for (cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(item, cmd->name))
            break;
    }

    if (cmd->name == NULL) {
        if (monitor_ctrl_mode(mon)) {
            qerror_report(QERR_COMMAND_NOT_FOUND, item);
            return qerror_new(QERR_UNDEFINED_ERROR);
        }
        goto help;
    }

    if (monitor_handler_is_async(cmd)) {
        if (monitor_ctrl_mode(mon)) {
            qmp_async_info_handler(mon, cmd);
        } else {
            user_async_info_handler(mon, cmd);
        }
        /*
         * Indicate that this command is asynchronous and will not return any
         * data (not even empty).  Instead, the data will be returned via a
         * completion callback.
         */
        *ret_data = qobject_from_jsonf("{ '__mon_async': 'return' }");
    } else if (monitor_handler_ported(cmd)) {
        cmd->mhandler.info_new(mon, ret_data);

        if (!monitor_ctrl_mode(mon)) {
            /*
             * User Protocol function is called here, Monitor Protocol is
             * handled by monitor_call_handler()
             */
            if (*ret_data)
                cmd->user_print(mon, *ret_data);
        }
    } else {
        if (monitor_ctrl_mode(mon)) {
            /* handler not converted yet */
            qerror_report(QERR_COMMAND_NOT_FOUND, item);
            return qerror_new(QERR_UNDEFINED_ERROR);
        } else {
            cmd->mhandler.info(mon);
        }
    }

    return 0;

help:
    help_cmd(mon, "info");
    return 0;
}

static QObject *get_cmd_dict(const char *name)
{
    const char *p;

    /* Remove '|' from some commands */
    p = strchr(name, '|');
    if (p) {
        p++;
    } else {
        p = name;
    }

    return qobject_from_jsonf("{ 'name': %s }", p);
}

/**
 * do_info_commands(): List QMP available commands
 *
 * Each command is represented by a QDict, the returned QObject is a QList
 * of all commands.
 *
 * The QDict contains:
 *
 * - "name": command's name
 *
 * Example:
 *
 * { [ { "name": "query-balloon" }, { "name": "system_powerdown" } ] }
 */
static void do_info_commands(Monitor *mon, QObject **ret_data)
{
    QList *cmd_list;
    const mon_cmd_t *cmd;

    cmd_list = qlist_new();

    for (cmd = mon_cmds; cmd->name != NULL; cmd++) {
        if (monitor_handler_ported(cmd) && !compare_cmd(cmd->name, "info")) {
            qlist_append_obj(cmd_list, get_cmd_dict(cmd->name));
        }
    }

    for (cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (monitor_handler_ported(cmd)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "query-%s", cmd->name);
            qlist_append_obj(cmd_list, get_cmd_dict(buf));
        }
    }

    *ret_data = QOBJECT(cmd_list);
}

/* get the current CPU defined by the user */
static int mon_set_cpu(int cpu_index)
{
    CPUState *env;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == cpu_index) {
            cur_mon->mon_cpu = env;
            return 0;
        }
    }
    return qerror_new(QERR_UNDEFINED_ERROR);
}

static CPUState *mon_get_cpu(void)
{
    if (!cur_mon->mon_cpu) {
        mon_set_cpu(0);
    }
    cpu_synchronize_state(cur_mon->mon_cpu);
    return cur_mon->mon_cpu;
}

static int do_cpu_set(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    int index = qdict_get_int(qdict, "index");
    if (mon_set_cpu(index) < 0) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "index",
                      "a CPU number");
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    return 0;
}

static void do_info_history(Monitor *mon)
{
    int i;
    const char *str;

    if (!mon->rs)
        return;
    i = 0;
    for(;;) {
        str = readline_get_history(mon->rs, i);
        if (!str)
            break;
        monitor_printf(mon, "%d: '%s'\n", i, str);
        i++;
    }
}

/**
 * do_quit(): Quit QEMU execution
 */
static int do_quit(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    monitor_suspend(mon);
    no_shutdown = 0;
    qemu_system_shutdown_request();

    return 0;
}

static void monitor_printc(Monitor *mon, int c)
{
    monitor_printf(mon, "'");
    switch(c) {
    case '\'':
        monitor_printf(mon, "\\'");
        break;
    case '\\':
        monitor_printf(mon, "\\\\");
        break;
    case '\n':
        monitor_printf(mon, "\\n");
        break;
    case '\r':
        monitor_printf(mon, "\\r");
        break;
    default:
        if (c >= 32 && c <= 126) {
            monitor_printf(mon, "%c", c);
        } else {
            monitor_printf(mon, "\\x%02x", c);
        }
        break;
    }
    monitor_printf(mon, "'");
}

static void memory_dump(Monitor *mon, int count, int format, int wsize,
                        target_phys_addr_t addr, int is_physical)
{
    CPUState *env;
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags;
        flags = 0;
        env = mon_get_cpu();
#ifdef TARGET_I386
        if (wsize == 2) {
            flags = 1;
        } else if (wsize == 4) {
            flags = 0;
        } else {
            /* as default we use the current CS size */
            flags = 0;
            if (env) {
#ifdef TARGET_X86_64
                if ((env->efer & MSR_EFER_LMA) &&
                    (env->segs[R_CS].flags & DESC_L_MASK))
                    flags = 2;
                else
#endif
                if (!(env->segs[R_CS].flags & DESC_B_MASK))
                    flags = 1;
            }
        }
#endif
        monitor_disas(mon, env, addr, count, is_physical, flags);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = (wsize * 8 + 2) / 3;
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = (wsize * 8 * 10 + 32) / 33;
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        if (is_physical)
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        else
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            cpu_physical_memory_rw(addr, buf, l, 0);
        } else {
            env = mon_get_cpu();
            if (cpu_memory_rw_debug(env, addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_raw(buf + i);
                break;
            case 2:
                v = lduw_raw(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_raw(buf + i);
                break;
            case 8:
                v = ldq_raw(buf + i);
                break;
            }
            monitor_printf(mon, " ");
            switch(format) {
            case 'o':
                monitor_printf(mon, "%#*" PRIo64, max_digits, v);
                break;
            case 'x':
                monitor_printf(mon, "0x%0*" PRIx64, max_digits, v);
                break;
            case 'u':
                monitor_printf(mon, "%*" PRIu64, max_digits, v);
                break;
            case 'd':
                monitor_printf(mon, "%*" PRId64, max_digits, v);
                break;
            case 'c':
                monitor_printc(mon, v);
                break;
            }
            i += wsize;
        }
        monitor_printf(mon, "\n");
        addr += l;
        len -= l;
    }
}

static void do_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_long addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 0);
}

static void do_physical_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_phys_addr_t addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 1);
}

static void do_print(Monitor *mon, const QDict *qdict)
{
    int format = qdict_get_int(qdict, "format");
    target_phys_addr_t val = qdict_get_int(qdict, "val");

#if TARGET_PHYS_ADDR_BITS == 32
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#o", val);
        break;
    case 'x':
        monitor_printf(mon, "%#x", val);
        break;
    case 'u':
        monitor_printf(mon, "%u", val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%d", val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#else
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" PRIo64, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" PRIx64, val);
        break;
    case 'u':
        monitor_printf(mon, "%" PRIu64, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" PRId64, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#endif
    monitor_printf(mon, "\n");
}

static void do_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint8_t buf[1];
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        cpu_physical_memory_rw(addr, buf, 1, 0);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += buf[0];
    }
    monitor_printf(mon, "%05d\n", sum);
}

static QObject *do_getfd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    mon_fd_t *monfd;
    int fd;

    fd = qemu_chr_get_msgfd(mon->chr);
    if (fd == -1) {
        return qerror_new(QERR_FD_NOT_SUPPLIED);
    }

    if (qemu_isdigit(fdname[0])) {
        return qerror_report(QERR_INVALID_PARAMETER_VALUE, "fdname",
                             "a name not starting with a digit");
    }

    QLIST_FOREACH(monfd, &mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        close(monfd->fd);
        monfd->fd = fd;
        return NULL;
    }

    monfd = qemu_mallocz(sizeof(mon_fd_t));
    monfd->name = qemu_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&mon->fds, monfd, next);
    return NULL;
}

static QObject *do_closefd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    mon_fd_t *monfd;

    QLIST_FOREACH(monfd, &mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        close(monfd->fd);
        qemu_free(monfd->name);
        qemu_free(monfd);
        return NULL;
    }

    return qerror_new(QERR_FD_NOT_FOUND, fdname);
}

int monitor_get_fd(Monitor *mon, const char *fdname)
{
    mon_fd_t *monfd;

    QLIST_FOREACH(monfd, &mon->fds, next) {
        int fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        fd = monfd->fd;

        /* caller takes ownership of fd */
        QLIST_REMOVE(monfd, next);
        qemu_free(monfd->name);
        qemu_free(monfd);

        return fd;
    }

    return -1;
}

static void do_info_registers(Monitor *mon)
{
    CPUState *env;
    env = mon_get_cpu();
#ifdef TARGET_I386
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   X86_DUMP_FPU);
#else
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   0);
#endif
}

static void do_info_jit(Monitor *mon)
{
    dump_exec_info((FILE *)mon, monitor_fprintf);
}

#if defined(TARGET_PPC)
/* XXX: not implemented in other targets */
static void do_info_cpu_stats(Monitor *mon)
{
    CPUState *env;

    env = mon_get_cpu();
    cpu_dump_statistics(env, (FILE *)mon, &monitor_fprintf, 0);
}
#endif

static const mon_cmd_t mon_cmds[] = {
#include "qemu-monitor.h"
    { NULL, NULL, },
};

/* Please update qemu-monitor.hx when adding or changing commands */
static const mon_cmd_t info_cmds[] = {
    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .user_print = do_info_version_print,
        .mhandler.info_new = do_info_version,
    },
    {
        .name       = "commands",
        .args_type  = "",
        .params     = "",
        .help       = "list QMP available commands",
        .user_print = monitor_user_noop,
        .mhandler.info_new = do_info_commands,
    },
    {
        .name       = "network",
        .args_type  = "",
        .params     = "",
        .help       = "show the network state",
        .mhandler.info = do_info_network,
    },
    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .user_print = qemu_chr_info_print,
        .mhandler.info_new = qemu_chr_info,
    },
    {
        .name       = "block",
        .args_type  = "",
        .params     = "",
        .help       = "show the block devices",
        .user_print = bdrv_info_print,
        .mhandler.info_new = bdrv_info,
    },
    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .user_print = bdrv_stats_print,
        .mhandler.info_new = bdrv_info_stats,
    },
    {
        .name       = "registers",
        .args_type  = "",
        .params     = "",
        .help       = "show the cpu registers",
        .mhandler.info = do_info_registers,
    },
    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .mhandler.info_new = do_info_cpus,
    },
    {
        .name       = "history",
        .args_type  = "",
        .params     = "",
        .help       = "show the command line history",
        .mhandler.info = do_info_history,
    },
    {
        .name       = "irq",
        .args_type  = "",
        .params     = "",
        .help       = "show the interrupts statistics (if available)",
        .mhandler.info = irq_info,
    },
    {
        .name       = "pic",
        .args_type  = "",
        .params     = "",
        .help       = "show i8259 (PIC) state",
        .mhandler.info = pic_info,
    },
    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .user_print = do_pci_info_print,
        .mhandler.info_new = do_pci_info,
    },
#if defined(TARGET_I386) || defined(TARGET_SH4)
    {
        .name       = "tlb",
        .args_type  = "",
        .params     = "",
        .help       = "show virtual to physical memory mappings",
        .mhandler.info = tlb_info,
    },
#endif
#if defined(TARGET_I386)
    {
        .name       = "mem",
        .args_type  = "",
        .params     = "",
        .help       = "show the active virtual memory mappings",
        .mhandler.info = mem_info,
    },
    {
        .name       = "hpet",
        .args_type  = "",
        .params     = "",
        .help       = "show state of HPET",
        .user_print = do_info_hpet_print,
        .mhandler.info_new = do_info_hpet,
    },
#endif
    {
        .name       = "jit",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler info",
        .mhandler.info = do_info_jit,
    },
    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .user_print = do_info_kvm_print,
        .mhandler.info_new = do_info_kvm,
    },
    {
        .name       = "numa",
        .args_type  = "",
        .params     = "",
        .help       = "show NUMA information",
        .mhandler.info = do_info_numa,
    },
    {
        .name       = "usb",
        .args_type  = "",
        .params     = "",
        .help       = "show guest USB devices",
        .mhandler.info = usb_info,
    },
    {
        .name       = "usbhost",
        .args_type  = "",
        .params     = "",
        .help       = "show host USB devices",
        .mhandler.info = usb_host_info,
    },
    {
        .name       = "profile",
        .args_type  = "",
        .params     = "",
        .help       = "show profiling information",
        .mhandler.info = do_info_profile,
    },
    {
        .name       = "capture",
        .args_type  = "",
        .params     = "",
        .help       = "show capture information",
        .mhandler.info = do_info_capture,
    },
    {
        .name       = "snapshots",
        .args_type  = "",
        .params     = "",
        .help       = "show the currently saved VM snapshots",
        .mhandler.info = do_info_snapshots,
    },
    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .user_print = do_info_status_print,
        .mhandler.info_new = do_info_status,
    },
    {
        .name       = "pcmcia",
        .args_type  = "",
        .params     = "",
        .help       = "show guest PCMCIA status",
        .mhandler.info = pcmcia_info,
    },
    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .user_print = do_info_mice_print,
        .mhandler.info_new = do_info_mice,
    },
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .user_print = do_info_vnc_print,
        .mhandler.info_new = do_info_vnc,
    },
    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .user_print = do_info_name_print,
        .mhandler.info_new = do_info_name,
    },
    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .user_print = do_info_uuid_print,
        .mhandler.info_new = do_info_uuid,
    },
#if defined(TARGET_PPC)
    {
        .name       = "cpustats",
        .args_type  = "",
        .params     = "",
        .help       = "show CPU statistics",
        .mhandler.info = do_info_cpu_stats,
    },
#endif
#if defined(CONFIG_SLIRP)
    {
        .name       = "usernet",
        .args_type  = "",
        .params     = "",
        .help       = "show user network stack connection states",
        .mhandler.info = do_info_usernet,
    },
#endif
    {
        .name       = "migrate",
        .args_type  = "",
        .params     = "",
        .help       = "show migration status",
        .user_print = do_info_migrate_print,
        .mhandler.info_new = do_info_migrate,
    },
    {
        .name       = "balloon",
        .args_type  = "",
        .params     = "",
        .help       = "show balloon information",
        .user_print = monitor_print_balloon,
        .mhandler.info_async = do_info_balloon,
        .async      = 1,
    },
    {
        .name       = "qtree",
        .args_type  = "",
        .params     = "",
        .help       = "show device tree",
        .mhandler.info = do_info_qtree,
    },
    {
        .name       = "qdm",
        .args_type  = "",
        .params     = "",
        .help       = "show qdev device model list",
        .mhandler.info = do_info_qdm,
    },
    {
        .name       = "roms",
        .args_type  = "",
        .params     = "",
        .help       = "show roms",
        .mhandler.info = do_info_roms,
    },
    {
        .name       = NULL,
    },
};

/*******************************************************************/

static const char *pch;
static jmp_buf expr_env;

#define MD_TLONG 0
#define MD_I32   1

typedef struct MonitorDef {
    const char *name;
    int offset;
    target_long (*get_value)(const struct MonitorDef *md, int val);
    int type;
} MonitorDef;

#if defined(TARGET_I386)
static target_long monitor_get_pc (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->eip + env->segs[R_CS].base;
}
#endif

#if defined(TARGET_PPC)
static target_long monitor_get_ccr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    unsigned int u;
    int i;

    u = 0;
    for (i = 0; i < 8; i++)
        u |= env->crf[i] << (32 - (4 * i));

    return u;
}

static target_long monitor_get_msr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->msr;
}

static target_long monitor_get_xer (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->xer;
}

static target_long monitor_get_decr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_tbl(env);
}
#endif

#if defined(TARGET_SPARC)
#ifndef TARGET_SPARC64
static target_long monitor_get_psr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();

    return cpu_get_psr(env);
}
#endif

static target_long monitor_get_reg(const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->regwptr[val];
}
#endif

static const MonitorDef monitor_defs[] = {
#ifdef TARGET_I386

#define SEG(name, seg) \
    { name, offsetof(CPUState, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUState, segs[seg].base) },\
    { name ".limit", offsetof(CPUState, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUState, regs[0]) },
    { "ecx", offsetof(CPUState, regs[1]) },
    { "edx", offsetof(CPUState, regs[2]) },
    { "ebx", offsetof(CPUState, regs[3]) },
    { "esp|sp", offsetof(CPUState, regs[4]) },
    { "ebp|fp", offsetof(CPUState, regs[5]) },
    { "esi", offsetof(CPUState, regs[6]) },
    { "edi", offsetof(CPUState, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUState, regs[8]) },
    { "r9", offsetof(CPUState, regs[9]) },
    { "r10", offsetof(CPUState, regs[10]) },
    { "r11", offsetof(CPUState, regs[11]) },
    { "r12", offsetof(CPUState, regs[12]) },
    { "r13", offsetof(CPUState, regs[13]) },
    { "r14", offsetof(CPUState, regs[14]) },
    { "r15", offsetof(CPUState, regs[15]) },
#endif
    { "eflags", offsetof(CPUState, eflags) },
    { "eip", offsetof(CPUState, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
#elif defined(TARGET_PPC)
    /* General purpose registers */
    { "r0", offsetof(CPUState, gpr[0]) },
    { "r1", offsetof(CPUState, gpr[1]) },
    { "r2", offsetof(CPUState, gpr[2]) },
    { "r3", offsetof(CPUState, gpr[3]) },
    { "r4", offsetof(CPUState, gpr[4]) },
    { "r5", offsetof(CPUState, gpr[5]) },
    { "r6", offsetof(CPUState, gpr[6]) },
    { "r7", offsetof(CPUState, gpr[7]) },
    { "r8", offsetof(CPUState, gpr[8]) },
    { "r9", offsetof(CPUState, gpr[9]) },
    { "r10", offsetof(CPUState, gpr[10]) },
    { "r11", offsetof(CPUState, gpr[11]) },
    { "r12", offsetof(CPUState, gpr[12]) },
    { "r13", offsetof(CPUState, gpr[13]) },
    { "r14", offsetof(CPUState, gpr[14]) },
    { "r15", offsetof(CPUState, gpr[15]) },
    { "r16", offsetof(CPUState, gpr[16]) },
    { "r17", offsetof(CPUState, gpr[17]) },
    { "r18", offsetof(CPUState, gpr[18]) },
    { "r19", offsetof(CPUState, gpr[19]) },
    { "r20", offsetof(CPUState, gpr[20]) },
    { "r21", offsetof(CPUState, gpr[21]) },
    { "r22", offsetof(CPUState, gpr[22]) },
    { "r23", offsetof(CPUState, gpr[23]) },
    { "r24", offsetof(CPUState, gpr[24]) },
    { "r25", offsetof(CPUState, gpr[25]) },
    { "r26", offsetof(CPUState, gpr[26]) },
    { "r27", offsetof(CPUState, gpr[27]) },
    { "r28", offsetof(CPUState, gpr[28]) },
    { "r29", offsetof(CPUState, gpr[29]) },
    { "r30", offsetof(CPUState, gpr[30]) },
    { "r31", offsetof(CPUState, gpr[31]) },
    /* Floating point registers */
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
    { "fpscr", offsetof(CPUState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUState, nip) },
    { "lr", offsetof(CPUState, lr) },
    { "ctr", offsetof(CPUState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "msr", 0, &monitor_get_msr, },
    { "xer", 0, &monitor_get_xer, },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
#if defined(TARGET_PPC64)
    /* Address space register */
    { "asr", offsetof(CPUState, asr) },
#endif
    /* Segment registers */
    { "sdr1", offsetof(CPUState, sdr1) },
    { "sr0", offsetof(CPUState, sr[0]) },
    { "sr1", offsetof(CPUState, sr[1]) },
    { "sr2", offsetof(CPUState, sr[2]) },
    { "sr3", offsetof(CPUState, sr[3]) },
    { "sr4", offsetof(CPUState, sr[4]) },
    { "sr5", offsetof(CPUState, sr[5]) },
    { "sr6", offsetof(CPUState, sr[6]) },
    { "sr7", offsetof(CPUState, sr[7]) },
    { "sr8", offsetof(CPUState, sr[8]) },
    { "sr9", offsetof(CPUState, sr[9]) },
    { "sr10", offsetof(CPUState, sr[10]) },
    { "sr11", offsetof(CPUState, sr[11]) },
    { "sr12", offsetof(CPUState, sr[12]) },
    { "sr13", offsetof(CPUState, sr[13]) },
    { "sr14", offsetof(CPUState, sr[14]) },
    { "sr15", offsetof(CPUState, sr[15]) },
    /* Too lazy to put BATs and SPRs ... */
#elif defined(TARGET_SPARC)
    { "g0", offsetof(CPUState, gregs[0]) },
    { "g1", offsetof(CPUState, gregs[1]) },
    { "g2", offsetof(CPUState, gregs[2]) },
    { "g3", offsetof(CPUState, gregs[3]) },
    { "g4", offsetof(CPUState, gregs[4]) },
    { "g5", offsetof(CPUState, gregs[5]) },
    { "g6", offsetof(CPUState, gregs[6]) },
    { "g7", offsetof(CPUState, gregs[7]) },
    { "o0", 0, monitor_get_reg },
    { "o1", 1, monitor_get_reg },
    { "o2", 2, monitor_get_reg },
    { "o3", 3, monitor_get_reg },
    { "o4", 4, monitor_get_reg },
    { "o5", 5, monitor_get_reg },
    { "o6", 6, monitor_get_reg },
    { "o7", 7, monitor_get_reg },
    { "l0", 8, monitor_get_reg },
    { "l1", 9, monitor_get_reg },
    { "l2", 10, monitor_get_reg },
    { "l3", 11, monitor_get_reg },
    { "l4", 12, monitor_get_reg },
    { "l5", 13, monitor_get_reg },
    { "l6", 14, monitor_get_reg },
    { "l7", 15, monitor_get_reg },
    { "i0", 16, monitor_get_reg },
    { "i1", 17, monitor_get_reg },
    { "i2", 18, monitor_get_reg },
    { "i3", 19, monitor_get_reg },
    { "i4", 20, monitor_get_reg },
    { "i5", 21, monitor_get_reg },
    { "i6", 22, monitor_get_reg },
    { "i7", 23, monitor_get_reg },
    { "pc", offsetof(CPUState, pc) },
    { "npc", offsetof(CPUState, npc) },
    { "y", offsetof(CPUState, y) },
#ifndef TARGET_SPARC64
    { "psr", 0, &monitor_get_psr, },
    { "wim", offsetof(CPUState, wim) },
#endif
    { "tbr", offsetof(CPUState, tbr) },
    { "fsr", offsetof(CPUState, fsr) },
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
#ifdef TARGET_SPARC64
    { "f32", offsetof(CPUState, fpr[32]) },
    { "f34", offsetof(CPUState, fpr[34]) },
    { "f36", offsetof(CPUState, fpr[36]) },
    { "f38", offsetof(CPUState, fpr[38]) },
    { "f40", offsetof(CPUState, fpr[40]) },
    { "f42", offsetof(CPUState, fpr[42]) },
    { "f44", offsetof(CPUState, fpr[44]) },
    { "f46", offsetof(CPUState, fpr[46]) },
    { "f48", offsetof(CPUState, fpr[48]) },
    { "f50", offsetof(CPUState, fpr[50]) },
    { "f52", offsetof(CPUState, fpr[52]) },
    { "f54", offsetof(CPUState, fpr[54]) },
    { "f56", offsetof(CPUState, fpr[56]) },
    { "f58", offsetof(CPUState, fpr[58]) },
    { "f60", offsetof(CPUState, fpr[60]) },
    { "f62", offsetof(CPUState, fpr[62]) },
    { "asi", offsetof(CPUState, asi) },
    { "pstate", offsetof(CPUState, pstate) },
    { "cansave", offsetof(CPUState, cansave) },
    { "canrestore", offsetof(CPUState, canrestore) },
    { "otherwin", offsetof(CPUState, otherwin) },
    { "wstate", offsetof(CPUState, wstate) },
    { "cleanwin", offsetof(CPUState, cleanwin) },
    { "fprs", offsetof(CPUState, fprs) },
#endif
#endif
    { NULL },
};

static void expr_error(Monitor *mon, const char *msg)
{
    monitor_printf(mon, "%s\n", msg);
    longjmp(expr_env, 1);
}

/* return 0 if OK, -1 if not found */
static int get_monitor_def(target_long *pval, const char *name)
{
    const MonitorDef *md;
    void *ptr;

    for(md = monitor_defs; md->name != NULL; md++) {
        if (compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md, md->offset);
            } else {
                CPUState *env = mon_get_cpu();
                ptr = (uint8_t *)env + md->offset;
                switch(md->type) {
                case MD_I32:
                    *pval = *(int32_t *)ptr;
                    break;
                case MD_TLONG:
                    *pval = *(target_long *)ptr;
                    break;
                default:
                    *pval = 0;
                    break;
                }
            }
            return 0;
        }
    }
    return qerror_new(QERR_UNDEFINED_ERROR);
}

static void next(void)
{
    if (*pch != '\0') {
        pch++;
        while (qemu_isspace(*pch))
            pch++;
    }
}

static int64_t expr_sum(Monitor *mon);

static int64_t expr_unary(Monitor *mon)
{
    int64_t n;
    char *p;
    int ret;

    switch(*pch) {
    case '+':
        next();
        n = expr_unary(mon);
        break;
    case '-':
        next();
        n = -expr_unary(mon);
        break;
    case '~':
        next();
        n = ~expr_unary(mon);
        break;
    case '(':
        next();
        n = expr_sum(mon);
        if (*pch != ')') {
            expr_error(mon, "')' expected");
        }
        next();
        break;
    case '\'':
        pch++;
        if (*pch == '\0')
            expr_error(mon, "character constant expected");
        n = *pch;
        pch++;
        if (*pch != '\'')
            expr_error(mon, "missing terminating \' character");
        next();
        break;
    case '$':
        {
            char buf[128], *q;
            target_long reg=0;

            pch++;
            q = buf;
            while ((*pch >= 'a' && *pch <= 'z') ||
                   (*pch >= 'A' && *pch <= 'Z') ||
                   (*pch >= '0' && *pch <= '9') ||
                   *pch == '_' || *pch == '.') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *pch;
                pch++;
            }
            while (qemu_isspace(*pch))
                pch++;
            *q = 0;
            ret = get_monitor_def(&reg, buf);
            if (ret < 0)
                expr_error(mon, "unknown register");
            n = reg;
        }
        break;
    case '\0':
        expr_error(mon, "unexpected end of expression");
        n = 0;
        break;
    default:
#if TARGET_PHYS_ADDR_BITS > 32
        n = strtoull(pch, &p, 0);
#else
        n = strtoul(pch, &p, 0);
#endif
        if (pch == p) {
            expr_error(mon, "invalid char in expression");
        }
        pch = p;
        while (qemu_isspace(*pch))
            pch++;
        break;
    }
    return n;
}


static int64_t expr_prod(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_unary(mon);
    for(;;) {
        op = *pch;
        if (op != '*' && op != '/' && op != '%')
            break;
        next();
        val2 = expr_unary(mon);
        switch(op) {
        default:
        case '*':
            val *= val2;
            break;
        case '/':
        case '%':
            if (val2 == 0)
                expr_error(mon, "division by zero");
            if (op == '/')
                val /= val2;
            else
                val %= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_logic(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_prod(mon);
    for(;;) {
        op = *pch;
        if (op != '&' && op != '|' && op != '^')
            break;
        next();
        val2 = expr_prod(mon);
        switch(op) {
        default:
        case '&':
            val &= val2;
            break;
        case '|':
            val |= val2;
            break;
        case '^':
            val ^= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_sum(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_logic(mon);
    for(;;) {
        op = *pch;
        if (op != '+' && op != '-')
            break;
        next();
        val2 = expr_logic(mon);
        if (op == '+')
            val += val2;
        else
            val -= val2;
    }
    return val;
}

static int get_expr(Monitor *mon, int64_t *pval, const char **pp)
{
    pch = *pp;
    if (setjmp(expr_env)) {
        *pp = pch;
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    while (qemu_isspace(*pch))
        pch++;
    *pval = expr_sum(mon);
    *pp = pch;
    return 0;
}

static int get_double(Monitor *mon, double *pval, const char **pp)
{
    const char *p = *pp;
    char *tailp;
    double d;

    d = strtod(p, &tailp);
    if (tailp == p) {
        monitor_printf(mon, "Number expected\n");
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    if (d != d || d - d != 0) {
        /* NaN or infinity */
        monitor_printf(mon, "Bad number\n");
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    *pval = d;
    *pp = tailp;
    return 0;
}

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    q = buf;
    p = *pp;
    while (qemu_isspace(*p))
        p++;
    if (*p == '\0') {
    fail:
        *q = '\0';
        *pp = p;
        return qerror_new(QERR_UNDEFINED_ERROR);
    }
    if (*p == '\"') {
        p++;
        while (*p != '\0' && *p != '\"') {
            if (*p == '\\') {
                p++;
                c = *p++;
                switch(c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    qemu_printf("unsupported escape code: '\\%c'\n", c);
                    goto fail;
                }
                if ((q - buf) < buf_size - 1) {
                    *q++ = c;
                }
            } else {
                if ((q - buf) < buf_size - 1) {
                    *q++ = *p;
                }
                p++;
            }
        }
        if (*p != '\"') {
            qemu_printf("unterminated string\n");
            goto fail;
        }
        p++;
    } else {
        while (*p != '\0' && !qemu_isspace(*p)) {
            if ((q - buf) < buf_size - 1) {
                *q++ = *p;
            }
            p++;
        }
    }
    *q = '\0';
    *pp = p;
    return 0;
}

/*
 * Store the command-name in cmdname, and return a pointer to
 * the remaining of the command string.
 */
static const char *get_command_name(const char *cmdline,
                                    char *cmdname, size_t nlen)
{
    size_t len;
    const char *p, *pstart;

    p = cmdline;
    while (qemu_isspace(*p))
        p++;
    if (*p == '\0')
        return NULL;
    pstart = p;
    while (*p != '\0' && *p != '/' && !qemu_isspace(*p))
        p++;
    len = p - pstart;
    if (len > nlen - 1)
        len = nlen - 1;
    memcpy(cmdname, pstart, len);
    cmdname[len] = '\0';
    return p;
}

/**
 * Read key of 'type' into 'key' and return the current
 * 'type' pointer.
 */
static char *key_get_info(const char *type, char **key)
{
    size_t len;
    char *p, *str;

    if (*type == ',')
        type++;

    p = strchr(type, ':');
    if (!p) {
        *key = NULL;
        return NULL;
    }
    len = p - type;

    str = qemu_malloc(len + 1);
    memcpy(str, type, len);
    str[len] = '\0';

    *key = str;
    return ++p;
}

static int default_fmt_format = 'x';
static int default_fmt_size = 4;

#define MAX_ARGS 16

static int is_valid_option(const char *c, const char *typestr)
{
    char option[3];
  
    option[0] = '-';
    option[1] = *c;
    option[2] = '\0';
  
    typestr = strstr(typestr, option);
    return (typestr != NULL);
}

static const mon_cmd_t *monitor_find_command(const char *cmdname)
{
    const mon_cmd_t *cmd;

    for (cmd = mon_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(cmdname, cmd->name)) {
            return cmd;
        }
    }

    return NULL;
}

static const mon_cmd_t *monitor_parse_command(Monitor *mon,
                                              const char *cmdline,
                                              QDict *qdict)
{
    const char *p, *typestr;
    int c;
    const mon_cmd_t *cmd;
    char cmdname[256];
    char buf[1024];
    char *key;

#ifdef DEBUG
    monitor_printf(mon, "command='%s'\n", cmdline);
#endif

    /* extract the command name */
    p = get_command_name(cmdline, cmdname, sizeof(cmdname));
    if (!p)
        return NULL;

    cmd = monitor_find_command(cmdname);
    if (!cmd) {
        monitor_printf(mon, "unknown command: '%s'\n", cmdname);
        return NULL;
    }

    /* parse the parameters */
    typestr = cmd->args_type;
    for(;;) {
        typestr = key_get_info(typestr, &key);
        if (!typestr)
            break;
        c = *typestr;
        typestr++;
        switch(c) {
        case 'F':
        case 'B':
        case 's':
            {
                int ret;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no optional string: NULL argument */
                        break;
                    }
                }
                ret = get_str(buf, sizeof(buf), &p);
                if (ret < 0) {
                    switch(c) {
                    case 'F':
                        monitor_printf(mon, "%s: filename expected\n",
                                       cmdname);
                        break;
                    case 'B':
                        monitor_printf(mon, "%s: block device name expected\n",
                                       cmdname);
                        break;
                    default:
                        monitor_printf(mon, "%s: string expected\n", cmdname);
                        break;
                    }
                    goto fail;
                }
                qdict_put(qdict, key, qstring_from_str(buf));
            }
            break;
        case 'O':
            {
                QemuOptsList *opts_list;
                QemuOpts *opts;

                opts_list = qemu_find_opts(key);
                if (!opts_list || opts_list->desc->name) {
                    goto bad_type;
                }
                while (qemu_isspace(*p)) {
                    p++;
                }
                if (!*p)
                    break;
                if (get_str(buf, sizeof(buf), &p) < 0) {
                    goto fail;
                }
                opts = qemu_opts_parse(opts_list, buf, 1);
                if (!opts) {
                    goto fail;
                }
                qemu_opts_to_qdict(opts, qdict);
                qemu_opts_del(opts);
            }
            break;
        case '/':
            {
                int count, format, size;

                while (qemu_isspace(*p))
                    p++;
                if (*p == '/') {
                    /* format found */
                    p++;
                    count = 1;
                    if (qemu_isdigit(*p)) {
                        count = 0;
                        while (qemu_isdigit(*p)) {
                            count = count * 10 + (*p - '0');
                            p++;
                        }
                    }
                    size = -1;
                    format = -1;
                    for(;;) {
                        switch(*p) {
                        case 'o':
                        case 'd':
                        case 'u':
                        case 'x':
                        case 'i':
                        case 'c':
                            format = *p++;
                            break;
                        case 'b':
                            size = 1;
                            p++;
                            break;
                        case 'h':
                            size = 2;
                            p++;
                            break;
                        case 'w':
                            size = 4;
                            p++;
                            break;
                        case 'g':
                        case 'L':
                            size = 8;
                            p++;
                            break;
                        default:
                            goto next;
                        }
                    }
                next:
                    if (*p != '\0' && !qemu_isspace(*p)) {
                        monitor_printf(mon, "invalid char in format: '%c'\n",
                                       *p);
                        goto fail;
                    }
                    if (format < 0)
                        format = default_fmt_format;
                    if (format != 'i') {
                        /* for 'i', not specifying a size gives -1 as size */
                        if (size < 0)
                            size = default_fmt_size;
                        default_fmt_size = size;
                    }
                    default_fmt_format = format;
                } else {
                    count = 1;
                    format = default_fmt_format;
                    if (format != 'i') {
                        size = default_fmt_size;
                    } else {
                        size = -1;
                    }
                }
                qdict_put(qdict, "count", qint_from_int(count));
                qdict_put(qdict, "format", qint_from_int(format));
                qdict_put(qdict, "size", qint_from_int(size));
            }
            break;
        case 'i':
        case 'l':
        case 'M':
            {
                int64_t val;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?' || *typestr == '.') {
                    if (*typestr == '?') {
                        if (*p == '\0') {
                            typestr++;
                            break;
                        }
                    } else {
                        if (*p == '.') {
                            p++;
                            while (qemu_isspace(*p))
                                p++;
                        } else {
                            typestr++;
                            break;
                        }
                    }
                    typestr++;
                }
                if (get_expr(mon, &val, &p))
                    goto fail;
                /* Check if 'i' is greater than 32-bit */
                if ((c == 'i') && ((val >> 32) & 0xffffffff)) {
                    monitor_printf(mon, "\'%s\' has failed: ", cmdname);
                    monitor_printf(mon, "integer is for 32-bit values\n");
                    goto fail;
                } else if (c == 'M') {
                    val <<= 20;
                }
                qdict_put(qdict, key, qint_from_int(val));
            }
            break;
        case 'f':
        case 'T':
            {
                double val;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        break;
                    }
                }
                if (get_double(mon, &val, &p) < 0) {
                    goto fail;
                }
                if (c == 'f' && *p) {
                    switch (*p) {
                    case 'K': case 'k':
                        val *= 1 << 10; p++; break;
                    case 'M': case 'm':
                        val *= 1 << 20; p++; break;
                    case 'G': case 'g':
                        val *= 1 << 30; p++; break;
                    }
                }
                if (c == 'T' && p[0] && p[1] == 's') {
                    switch (*p) {
                    case 'm':
                        val /= 1e3; p += 2; break;
                    case 'u':
                        val /= 1e6; p += 2; break;
                    case 'n':
                        val /= 1e9; p += 2; break;
                    }
                }
                if (*p && !qemu_isspace(*p)) {
                    monitor_printf(mon, "Unknown unit suffix\n");
                    goto fail;
                }
                qdict_put(qdict, key, qfloat_from_double(val));
            }
            break;
        case 'b':
            {
                const char *beg;
                int val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                beg = p;
                while (qemu_isgraph(*p)) {
                    p++;
                }
                if (p - beg == 2 && !memcmp(beg, "on", p - beg)) {
                    val = 1;
                } else if (p - beg == 3 && !memcmp(beg, "off", p - beg)) {
                    val = 0;
                } else {
                    monitor_printf(mon, "Expected 'on' or 'off'\n");
                    goto fail;
                }
                qdict_put(qdict, key, qbool_from_int(val));
            }
            break;
        case '-':
            {
                const char *tmp = p;
                int has_option, skip_key = 0;
                /* option */

                c = *typestr++;
                if (c == '\0')
                    goto bad_type;
                while (qemu_isspace(*p))
                    p++;
                has_option = 0;
                if (*p == '-') {
                    p++;
                    if(c != *p) {
                        if(!is_valid_option(p, typestr)) {
                  
                            monitor_printf(mon, "%s: unsupported option -%c\n",
                                           cmdname, *p);
                            goto fail;
                        } else {
                            skip_key = 1;
                        }
                    }
                    if(skip_key) {
                        p = tmp;
                    } else {
                        p++;
                        has_option = 1;
                    }
                }
                qdict_put(qdict, key, qint_from_int(has_option));
            }
            break;
        default:
        bad_type:
            monitor_printf(mon, "%s: unknown type '%c'\n", cmdname, c);
            goto fail;
        }
        qemu_free(key);
        key = NULL;
    }
    /* check that all arguments were parsed */
    while (qemu_isspace(*p))
        p++;
    if (*p != '\0') {
        monitor_printf(mon, "%s: extraneous characters at the end of line\n",
                       cmdname);
        goto fail;
    }

    return cmd;

fail:
    qemu_free(key);
    return NULL;
}

void monitor_set_error(Monitor *mon, QError *qerror)
{
    /* report only the first error */
    if (!mon->error) {
        mon->error = qerror;
    } else {
        MON_DEBUG("Additional error report at %s:%d\n",
                  qerror->file, qerror->linenr);
        QDECREF(qerror);
    }
}

static int is_async_return(const QObject *data)
{
    if (data && qobject_type(data) == QTYPE_QDICT) {
        return qdict_haskey(qobject_to_qdict(data), "__mon_async");
    }

    return 0;
}

static void handler_audit(Monitor *mon, const mon_cmd_t *cmd, int ret)
{
    if (monitor_ctrl_mode(mon)) {
        if (ret && !monitor_has_error(mon)) {
            /*
             * If it returns failure, it must have passed on error.
             *
             * Action: Report an internal error to the client if in QMP.
             */
            qerror_report(QERR_UNDEFINED_ERROR);
            MON_DEBUG("command '%s' returned failure but did not pass an error\n",
                      cmd->name);
        }

#ifdef CONFIG_DEBUG_MONITOR
        if (!ret && monitor_has_error(mon)) {
            /*
             * If it returns success, it must not have passed an error.
             *
             * Action: Report the passed error to the client.
             */
            MON_DEBUG("command '%s' returned success but passed an error\n",
                      cmd->name);
        }

        if (mon_print_count_get(mon) > 0 && strcmp(cmd->name, "info") != 0) {
            /*
             * Handlers should not call Monitor print functions.
             *
             * Action: Ignore them in QMP.
             *
             * (XXX: we don't check any 'info' or 'query' command here
             * because the user print function _is_ called by do_info(), hence
             * we will trigger this check. This problem will go away when we
             * make 'query' commands real and kill do_info())
             */
            MON_DEBUG("command '%s' called print functions %d time(s)\n",
                      cmd->name, mon_print_count_get(mon));
        }
#endif
    } else {
        assert(!monitor_has_error(mon));
        QDECREF(mon->error);
        mon->error = NULL;
    }
}

static void monitor_call_handler(Monitor *mon, const mon_cmd_t *cmd,
                                 const QDict *params, QString *id)
{
    int ret;
    QObject *data = NULL;

    mon_print_count_init(mon);

    ret = cmd->mhandler.cmd_new(mon, params, &data);
    handler_audit(mon, cmd, ret);

    if (is_async_return(data)) {
        /*
         * Asynchronous commands have no initial return data but they can
         * generate errors.  Data is returned via the async completion handler.
         */
        if (monitor_ctrl_mode(mon) && monitor_has_error(mon)) {
            monitor_protocol_emitter(mon, NULL, id);
        }
    } else if (monitor_ctrl_mode(mon)) {
        /* Monitor Protocol */
        monitor_protocol_emitter(mon, data, id);
    } else {
        /* User Protocol */
         if (data)
            cmd->user_print(mon, data);
    }

    qobject_decref(data);
}

static void handle_user_command(Monitor *mon, const char *cmdline)
{
    QDict *qdict;
    const mon_cmd_t *cmd;

    qdict = qdict_new();

    cmd = monitor_parse_command(mon, cmdline, qdict);
    if (!cmd)
        goto out;

    if (monitor_handler_is_async(cmd)) {
        user_async_cmd_handler(mon, cmd, qdict);
    } else if (monitor_handler_ported(cmd)) {
        monitor_call_handler(mon, cmd, qdict, NULL);
    } else {
        cmd->mhandler.cmd(mon, qdict);
    }

out:
    QDECREF(qdict);
}

static void cmd_completion(const char *name, const char *list)
{
    const char *p, *pstart;
    char cmd[128];
    int len;

    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        len = p - pstart;
        if (len > sizeof(cmd) - 2)
            len = sizeof(cmd) - 2;
        memcpy(cmd, pstart, len);
        cmd[len] = '\0';
        if (name[0] == '\0' || !strncmp(name, cmd, strlen(name))) {
            readline_add_completion(cur_mon->rs, cmd);
        }
        if (*p == '\0')
            break;
        p++;
    }
}

static void file_completion(const char *input)
{
    DIR *ffs;
    struct dirent *d;
    char path[1024];
    char file[1024], file_prefix[1024];
    int input_path_len;
    const char *p;

    p = strrchr(input, '/');
    if (!p) {
        input_path_len = 0;
        pstrcpy(file_prefix, sizeof(file_prefix), input);
        pstrcpy(path, sizeof(path), ".");
    } else {
        input_path_len = p - input + 1;
        memcpy(path, input, input_path_len);
        if (input_path_len > sizeof(path) - 1)
            input_path_len = sizeof(path) - 1;
        path[input_path_len] = '\0';
        pstrcpy(file_prefix, sizeof(file_prefix), p + 1);
    }
#ifdef DEBUG_COMPLETION
    monitor_printf(cur_mon, "input='%s' path='%s' prefix='%s'\n",
                   input, path, file_prefix);
#endif
    ffs = opendir(path);
    if (!ffs)
        return;
    for(;;) {
        struct stat sb;
        d = readdir(ffs);
        if (!d)
            break;
        if (strstart(d->d_name, file_prefix, NULL)) {
            memcpy(file, input, input_path_len);
            if (input_path_len < sizeof(file))
                pstrcpy(file + input_path_len, sizeof(file) - input_path_len,
                        d->d_name);
            /* stat the file to find out if it's a directory.
             * In that case add a slash to speed up typing long paths
             */
            stat(file, &sb);
            if(S_ISDIR(sb.st_mode))
                pstrcat(file, sizeof(file), "/");
            readline_add_completion(cur_mon->rs, file);
        }
    }
    closedir(ffs);
}

static void block_completion_it(void *opaque, BlockDriverState *bs)
{
    const char *name = bdrv_get_device_name(bs);
    const char *input = opaque;

    if (input[0] == '\0' ||
        !strncmp(name, (char *)input, strlen(input))) {
        readline_add_completion(cur_mon->rs, name);
    }
}

/* NOTE: this parser is an approximate form of the real command parser */
static void parse_cmdline(const char *cmdline,
                         int *pnb_args, char **args)
{
    const char *p;
    int nb_args, ret;
    char buf[1024];

    p = cmdline;
    nb_args = 0;
    for(;;) {
        while (qemu_isspace(*p))
            p++;
        if (*p == '\0')
            break;
        if (nb_args >= MAX_ARGS)
            break;
        ret = get_str(buf, sizeof(buf), &p);
        args[nb_args] = qemu_strdup(buf);
        nb_args++;
        if (ret < 0)
            break;
    }
    *pnb_args = nb_args;
}

static const char *next_arg_type(const char *typestr)
{
    const char *p = strchr(typestr, ':');
    return (p != NULL ? ++p : typestr);
}

static void monitor_find_completion(const char *cmdline)
{
    const char *cmdname;
    char *args[MAX_ARGS];
    int nb_args, i, len;
    const char *ptype, *str;
    const mon_cmd_t *cmd;
    const KeyDef *key;

    parse_cmdline(cmdline, &nb_args, args);
#ifdef DEBUG_COMPLETION
    for(i = 0; i < nb_args; i++) {
        monitor_printf(cur_mon, "arg%d = '%s'\n", i, (char *)args[i]);
    }
#endif

    /* if the line ends with a space, it means we want to complete the
       next arg */
    len = strlen(cmdline);
    if (len > 0 && qemu_isspace(cmdline[len - 1])) {
        if (nb_args >= MAX_ARGS)
            return;
        args[nb_args++] = qemu_strdup("");
    }
    if (nb_args <= 1) {
        /* command completion */
        if (nb_args == 0)
            cmdname = "";
        else
            cmdname = args[0];
        readline_set_completion_index(cur_mon->rs, strlen(cmdname));
        for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
            cmd_completion(cmdname, cmd->name);
        }
    } else {
        /* find the command */
        for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
            if (compare_cmd(args[0], cmd->name))
                goto found;
        }
        return;
    found:
        ptype = next_arg_type(cmd->args_type);
        for(i = 0; i < nb_args - 2; i++) {
            if (*ptype != '\0') {
                ptype = next_arg_type(ptype);
                while (*ptype == '?')
                    ptype = next_arg_type(ptype);
            }
        }
        str = args[nb_args - 1];
        if (*ptype == '-' && ptype[1] != '\0') {
            ptype += 2;
        }
        switch(*ptype) {
        case 'F':
            /* file completion */
            readline_set_completion_index(cur_mon->rs, strlen(str));
            file_completion(str);
            break;
        case 'B':
            /* block device name completion */
            readline_set_completion_index(cur_mon->rs, strlen(str));
            bdrv_iterate(block_completion_it, (void *)str);
            break;
        case 's':
            /* XXX: more generic ? */
            if (!strcmp(cmd->name, "info")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(cmd = info_cmds; cmd->name != NULL; cmd++) {
                    cmd_completion(str, cmd->name);
                }
            } else if (!strcmp(cmd->name, "sendkey")) {
                char *sep = strrchr(str, '-');
                if (sep)
                    str = sep + 1;
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(key = key_defs; key->name != NULL; key++) {
                    cmd_completion(str, key->name);
                }
            } else if (!strcmp(cmd->name, "help|?")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for (cmd = mon_cmds; cmd->name != NULL; cmd++) {
                    cmd_completion(str, cmd->name);
                }
            }
            break;
        default:
            break;
        }
    }
    for(i = 0; i < nb_args; i++)
        qemu_free(args[i]);
}

static int monitor_can_read(void *opaque)
{
    Monitor *mon = opaque;

    return (mon->suspend_cnt == 0) ? 1 : 0;
}

typedef struct CmdArgs {
    QString *name;
    int type;
    int flag;
    int optional;
} CmdArgs;

static int check_opt(const CmdArgs *cmd_args, const char *name, QDict *args)
{
    if (!cmd_args->optional) {
        qerror_report(QERR_MISSING_PARAMETER, name);
        return qerror_new(QERR_UNDEFINED_ERROR);
    }

    if (cmd_args->type == '-') {
        /* handlers expect a value, they need to be changed */
        qdict_put(args, name, qint_from_int(0));
    }

    return 0;
}

static int check_arg(const CmdArgs *cmd_args, QDict *args)
{
    QObject *value;
    const char *name;

    name = qstring_get_str(cmd_args->name);

    if (!args) {
        return check_opt(cmd_args, name, args);
    }

    value = qdict_get(args, name);
    if (!value) {
        return check_opt(cmd_args, name, args);
    }

    switch (cmd_args->type) {
        case 'F':
        case 'B':
        case 's':
            if (qobject_type(value) != QTYPE_QSTRING) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "string");
                return qerror_new(QERR_UNDEFINED_ERROR);
            }
            break;
        case '/': {
            int i;
            const char *keys[] = { "count", "format", "size", NULL };

            for (i = 0; keys[i]; i++) {
                QObject *obj = qdict_get(args, keys[i]);
                if (!obj) {
                    qerror_report(QERR_MISSING_PARAMETER, name);
                    return qerror_new(QERR_UNDEFINED_ERROR);
                }
                if (qobject_type(obj) != QTYPE_QINT) {
                    qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "int");
                    return qerror_new(QERR_UNDEFINED_ERROR);
                }
            }
            break;
        }
        case 'i':
        case 'l':
        case 'M':
            if (qobject_type(value) != QTYPE_QINT) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "int");
                return qerror_new(QERR_UNDEFINED_ERROR);
            }
            break;
        case 'f':
        case 'T':
            if (qobject_type(value) != QTYPE_QINT && qobject_type(value) != QTYPE_QFLOAT) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "number");
                return qerror_new(QERR_UNDEFINED_ERROR);
            }
            break;
        case 'b':
            if (qobject_type(value) != QTYPE_QBOOL) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "bool");
                return qerror_new(QERR_UNDEFINED_ERROR);
            }
            break;
        case '-':
            if (qobject_type(value) != QTYPE_QINT &&
                qobject_type(value) != QTYPE_QBOOL) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, name, "bool");
                return qerror_new(QERR_UNDEFINED_ERROR);
            }
            if (qobject_type(value) == QTYPE_QBOOL) {
                /* handlers expect a QInt, they need to be changed */
                qdict_put(args, name,
                         qint_from_int(qbool_get_int(qobject_to_qbool(value))));
            }
            break;
        case 'O':
        default:
            /* impossible */
            abort();
    }

    return 0;
}

static void cmd_args_init(CmdArgs *cmd_args)
{
    cmd_args->name = qstring_new();
    cmd_args->type = cmd_args->flag = cmd_args->optional = 0;
}

static int check_opts(QemuOptsList *opts_list, QDict *args)
{
    assert(!opts_list->desc->name);
    return 0;
}

/*
 * This is not trivial, we have to parse Monitor command's argument
 * type syntax to be able to check the arguments provided by clients.
 *
 * In the near future we will be using an array for that and will be
 * able to drop all this parsing...
 */
static int monitor_check_qmp_args(const mon_cmd_t *cmd, QDict *args)
{
    int err;
    const char *p;
    CmdArgs cmd_args;
    QemuOptsList *opts_list;

    if (cmd->args_type == NULL) {
        return (qdict_size(args) == 0 ? 0 : -1);
    }

    err = 0;
    cmd_args_init(&cmd_args);
    opts_list = NULL;

    for (p = cmd->args_type;; p++) {
        if (*p == ':') {
            cmd_args.type = *++p;
            p++;
            if (cmd_args.type == '-') {
                cmd_args.flag = *p++;
                cmd_args.optional = 1;
            } else if (cmd_args.type == 'O') {
                opts_list = qemu_find_opts(qstring_get_str(cmd_args.name));
                assert(opts_list);
            } else if (*p == '?') {
                cmd_args.optional = 1;
                p++;
            }

            assert(*p == ',' || *p == '\0');
            if (opts_list) {
                err = check_opts(opts_list, args);
                opts_list = NULL;
            } else {
                err = check_arg(&cmd_args, args);
                QDECREF(cmd_args.name);
                cmd_args_init(&cmd_args);
            }

            if (err < 0) {
                break;
            }
        } else {
            qstring_append_chr(cmd_args.name, *p);
        }

        if (*p == '\0') {
            break;
        }
    }

    QDECREF(cmd_args.name);
    return err;
}

static int invalid_qmp_mode(const Monitor *mon, const char *cmd_name)
{
    int is_cap = compare_cmd(cmd_name, "qmp_capabilities");
    return (qmp_cmd_mode(mon) ? is_cap : !is_cap);
}

static void handle_qmp_command(JSONMessageParser *parser, QList *tokens)
{
    int err;
    QObject *obj;
    QDict *input, *args;
    const mon_cmd_t *cmd;
    Monitor *mon = cur_mon;
    const char *cmd_name, *info_item;
    QString *id = NULL;

    args = NULL;

    obj = json_parser_parse(tokens);
    if (!obj) {
        // FIXME: should be triggered in json_parser_parse()
        qerror_report(QERR_JSON_PARSING);
        goto out;
    } else if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT, "object");
        qobject_decref(obj);
        goto out;
    }

    input = qobject_to_qdict(obj);

    id = qobject_to_qstring(qdict_get(input, "id"));
    QINCREF(id);

    obj = qdict_get(input, "execute");
    if (!obj) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT, "execute");
        goto err_input;
    } else if (qobject_type(obj) != QTYPE_QSTRING) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "execute", "string");
        goto err_input;
    }

    cmd_name = qstring_get_str(qobject_to_qstring(obj));

    if (invalid_qmp_mode(mon, cmd_name)) {
        qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_input;
    }

    /*
     * XXX: We need this special case until we get info handlers
     * converted into 'query-' commands
     */
    if (compare_cmd(cmd_name, "info")) {
        qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_input;
    } else if (strstart(cmd_name, "query-", &info_item)) {
        cmd = monitor_find_command("info");
        qdict_put_obj(input, "arguments",
                      qobject_from_jsonf("{ 'item': %s }", info_item));
    } else {
        cmd = monitor_find_command(cmd_name);
        if (!cmd || !monitor_handler_ported(cmd)) {
            qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
            goto err_input;
        }
    }

    obj = qdict_get(input, "arguments");
    if (!obj) {
        args = qdict_new();
    } else if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "arguments", "object");
        goto err_input;
    } else {
        args = qobject_to_qdict(obj);
        QINCREF(args);
    }

    QDECREF(input);

    err = monitor_check_qmp_args(cmd, args);
    if (err < 0) {
        goto err_out;
    }

    if (monitor_handler_is_async(cmd)) {
        qmp_async_cmd_handler(mon, cmd, args, id);
    } else {
        monitor_call_handler(mon, cmd, args, id);
    }
    goto out;

err_input:
    QDECREF(input);
err_out:
    monitor_protocol_emitter(mon, NULL, id);
out:
    QDECREF(args);
}

/**
 * monitor_control_read(): Read and handle QMP input
 */
static void monitor_control_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;

    cur_mon = opaque;

    json_message_parser_feed(&cur_mon->mc->parser, (const char *) buf, size);

    cur_mon = old_mon;
}

static void monitor_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;
    int i;

    cur_mon = opaque;

    if (cur_mon->rs) {
        for (i = 0; i < size; i++)
            readline_handle_byte(cur_mon->rs, buf[i]);
    } else {
        if (size == 0 || buf[size - 1] != 0)
            monitor_printf(cur_mon, "corrupted command\n");
        else
            handle_user_command(cur_mon, (char *)buf);
    }

    cur_mon = old_mon;
}

static void monitor_command_cb(Monitor *mon, const char *cmdline, void *opaque)
{
    monitor_suspend(mon);
    handle_user_command(mon, cmdline);
    monitor_resume(mon);
}

int monitor_suspend(Monitor *mon)
{
    if (!mon->rs)
        return -ENOTTY;
    mon->suspend_cnt++;
    return 0;
}

void monitor_resume(Monitor *mon)
{
    if (!mon->rs)
        return;
    if (--mon->suspend_cnt == 0)
        readline_show_prompt(mon->rs);
}

static QObject *get_qmp_greeting(void)
{
    QObject *ver;

    do_info_version(NULL, &ver);
    return qobject_from_jsonf("{'QMP':{'version': %p,'capabilities': []}}",ver);
}

/**
 * monitor_control_event(): Print QMP gretting
 */
static void monitor_control_event(void *opaque, int event)
{
    QObject *data;
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        mon->mc->command_mode = 0;
        json_message_parser_init(&mon->mc->parser, handle_qmp_command);
        data = get_qmp_greeting();
        monitor_json_emitter(mon, data);
        qobject_decref(data);
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_destroy(&mon->mc->parser);
        break;
    }
}

static void monitor_event(void *opaque, int event)
{
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_MUX_IN:
        mon->mux_out = 0;
        if (mon->reset_seen) {
            readline_restart(mon->rs);
            monitor_resume(mon);
            monitor_flush(mon);
        } else {
            mon->suspend_cnt = 0;
        }
        break;

    case CHR_EVENT_MUX_OUT:
        if (mon->reset_seen) {
            if (mon->suspend_cnt == 0) {
                monitor_printf(mon, "\n");
            }
            monitor_flush(mon);
            monitor_suspend(mon);
        } else {
            mon->suspend_cnt++;
        }
        mon->mux_out = 1;
        break;

    case CHR_EVENT_OPENED:
        monitor_printf(mon, "QEMU %s monitor - type 'help' for more "
                       "information\n", QEMU_VERSION);
        if (!mon->mux_out) {
            readline_show_prompt(mon->rs);
        }
        mon->reset_seen = 1;
        break;
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */

void monitor_init(CharDriverState *chr, int flags)
{
    static int is_first_init = 1;
    Monitor *mon;

    if (is_first_init) {
        key_timer = qemu_new_timer(vm_clock, release_keys, NULL);
        is_first_init = 0;
    }

    mon = qemu_mallocz(sizeof(*mon));

    mon->chr = chr;
    mon->flags = flags;
    if (flags & MONITOR_USE_READLINE) {
        mon->rs = readline_init(mon, monitor_find_completion);
        monitor_read_command(mon, 0);
    }

    if (monitor_ctrl_mode(mon)) {
        mon->mc = qemu_mallocz(sizeof(MonitorControl));
        /* Control mode requires special handlers */
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_control_read,
                              monitor_control_event, mon);
    } else {
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_read,
                              monitor_event, mon);
    }

    QLIST_INSERT_HEAD(&mon_list, mon, entry);
    if (!default_mon || (flags & MONITOR_IS_DEFAULT))
        default_mon = mon;
}

static void bdrv_password_cb(Monitor *mon, const char *password, void *opaque)
{
    BlockDriverState *bs = opaque;
    int ret = 0;

    if (bdrv_set_key(bs, password) != 0) {
        monitor_printf(mon, "invalid password\n");
        ret = -EPERM;
    }
    if (mon->password_completion_cb)
        mon->password_completion_cb(mon->password_opaque, ret);

    monitor_read_command(mon, 1);
}

QObject *monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                     BlockDriverCompletionFunc *completion_cb,
                                     void *opaque)
{
    int err;

    if (!bdrv_key_required(bs)) {
        if (completion_cb)
            completion_cb(opaque, 0);
        return NULL;
    }

    if (monitor_ctrl_mode(mon)) {
        qerror_report(QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs));
        return qerror_new(QERR_UNDEFINED_ERROR);
    }

    monitor_printf(mon, "%s (%s) is encrypted.\n", bdrv_get_device_name(bs),
                   bdrv_get_encrypted_filename(bs));

    mon->password_completion_cb = completion_cb;
    mon->password_opaque = opaque;

    err = monitor_read_password(mon, bdrv_password_cb, bs);

    if (err && completion_cb)
        completion_cb(opaque, err);

    if (err) {
        return qerror_new(QERR_UNDEFINED_ERROR);
    }

    return NULL;
}
