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
#include "qlist.h"
#include "qdict.h"
#include "qbool.h"
#include "qstring.h"
#include "qerror.h"
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
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for all types, except '/')
 * '.'          other form of optional type (for 'i' and 'l')
 * '-'          optional parameter (eg. '-f')
 *
 */

typedef struct MonitorCommandHandler {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    UserPrintHandler *user_print;
    union {
        LegacyInfoHandler *info;
        InfoHandler *info_new;
        LegacyCommandHandler *cmd;
        CommandHandler *cmd_new;
        void *callback;
    } mhandler;
    QTAILQ_ENTRY(MonitorCommandHandler) node;
} MonitorCommandHandler;

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

typedef struct MonitorControl {
    QObject *id;
    int print_enabled;
    JSONMessageParser parser;
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
    QError *error;
    QLIST_HEAD(,mon_fd_t) fds;
    QLIST_ENTRY(Monitor) entry;
};

static QLIST_HEAD(mon_list, Monitor) mon_list;

typedef QTAILQ_HEAD(MonitorCommandList, MonitorCommandHandler) MonitorCommandList;

static MonitorCommandList mon_cmds = QTAILQ_HEAD_INITIALIZER(mon_cmds);
static MonitorCommandList info_cmds = QTAILQ_HEAD_INITIALIZER(info_cmds);

Monitor *cur_mon = NULL;

static void monitor_command_cb(Monitor *mon, const char *cmdline,
                               void *opaque);

/* Return true if in control mode, false otherwise */
int monitor_ctrl_mode(const Monitor *mon)
{
    return (mon->flags & MONITOR_USE_CONTROL);
}

static void monitor_read_command(Monitor *mon, int show_prompt)
{
    if (!mon->rs)
        return;

    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt)
        readline_show_prompt(mon->rs);
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
    if (!mon)
        return;

    if (mon->mc && !mon->mc->print_enabled) {
        qemu_error_new(QERR_UNDEFINED_ERROR);
    } else {
        char buf[4096];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        monitor_puts(mon, buf);
    }
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

static void monitor_user_noop(Monitor *mon, const QObject *data)
{
}

static inline int monitor_handler_ported(const MonitorCommandHandler *cmd)
{
    return cmd->user_print != NULL;
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

    mon->mc->print_enabled = 1;
    monitor_printf(mon, "%s\n", qstring_get_str(json));
    mon->mc->print_enabled = 0;

    QDECREF(json);
}

static void monitor_protocol_emitter(Monitor *mon, QObject *data)
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

    if (mon->mc->id) {
        qdict_put_obj(qmp, "id", mon->mc->id);
        mon->mc->id = NULL;
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
    assert(obj != NULL);

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
        case QEVENT_DEBUG:
            event_name = "DEBUG";
            break;
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
        case QEVENT_VNC_CONNECTED:
            event_name = "VNC_CONNECTED";
            break;
        case QEVENT_VNC_INITIALIZED:
            event_name = "VNC_INITIALIZED";
            break;
        case QEVENT_VNC_DISCONNECTED:
            event_name = "VNC_DISCONNECTED";
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
        if (monitor_ctrl_mode(mon)) {
            monitor_json_emitter(mon, QOBJECT(qmp));
        }
    }
    QDECREF(qmp);
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

static void help_cmd_dump(Monitor *mon, const MonitorCommandList *cmds,
                          const char *prefix, const char *name)
{
    const MonitorCommandHandler *cmd;

    QTAILQ_FOREACH(cmd, cmds, node) {
        if (!name || !strcmp(name, cmd->name))
            monitor_printf(mon, "%s%s %s -- %s\n", prefix, cmd->name,
                           cmd->params, cmd->help);
    }
}

static void help_cmd(Monitor *mon, const char *name)
{
    if (name && !strcmp(name, "info")) {
        help_cmd_dump(mon, &info_cmds, "info ", NULL);
    } else {
        help_cmd_dump(mon, &mon_cmds, "", name);
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

static void do_info(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const MonitorCommandHandler *cmd;
    const char *item = qdict_get_try_str(qdict, "item");

    if (!item) {
        assert(monitor_ctrl_mode(mon) == 0);
        goto help;
    }

    QTAILQ_FOREACH(cmd, &info_cmds, node) {
        if (compare_cmd(item, cmd->name))
            break;
    }

    if (cmd == NULL) {
        if (monitor_ctrl_mode(mon)) {
            qemu_error_new(QERR_COMMAND_NOT_FOUND, item);
            return;
        }
        goto help;
    }

    if (monitor_handler_ported(cmd)) {
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
            qemu_error_new(QERR_COMMAND_NOT_FOUND, item);
        } else {
            cmd->mhandler.info(mon);
        }
    }

    return;

help:
    help_cmd(mon, "info");
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
    const MonitorCommandHandler *cmd;

    cmd_list = qlist_new();

    QTAILQ_FOREACH(cmd, &mon_cmds, node) {
        if (monitor_handler_ported(cmd) && !compare_cmd(cmd->name, "info")) {
            qlist_append_obj(cmd_list, get_cmd_dict(cmd->name));
        }
    }

    QTAILQ_FOREACH(cmd, &info_cmds, node) {
        if (monitor_handler_ported(cmd)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "query-%s", cmd->name);
            qlist_append_obj(cmd_list, get_cmd_dict(buf));
        }
    }

    *ret_data = QOBJECT(cmd_list);
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

static void do_getfd(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    mon_fd_t *monfd;
    int fd;

    fd = qemu_chr_get_msgfd(mon->chr);
    if (fd == -1) {
        qemu_error_new(QERR_FD_NOT_SUPPLIED);
        return;
    }

    if (qemu_isdigit(fdname[0])) {
        qemu_error_new(QERR_INVALID_PARAMETER, "fdname");
        return;
    }

    fd = dup(fd);
    if (fd == -1) {
        if (errno == EMFILE)
            qemu_error_new(QERR_TOO_MANY_FILES);
        else
            qemu_error_new(QERR_UNDEFINED_ERROR);
        return;
    }

    QLIST_FOREACH(monfd, &mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        close(monfd->fd);
        monfd->fd = fd;
        return;
    }

    monfd = qemu_mallocz(sizeof(mon_fd_t));
    monfd->name = qemu_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&mon->fds, monfd, next);
}

static void do_closefd(Monitor *mon, const QDict *qdict, QObject **ret_data)
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
        return;
    }

    qemu_error_new(QERR_FD_NOT_FOUND, fdname);
}

static void do_register_cmd(MonitorCommandList *list,
                            const char *name,
                            const char *args_type,
                            const char *params,
                            const char *help,
                            UserPrintHandler *user_print,
                            void *cmd)
{
    MonitorCommandHandler *handler;

    handler = qemu_mallocz(sizeof(*handler));
    handler->name = name;
    handler->args_type = args_type;
    handler->params = params;
    handler->help = help;
    handler->user_print = user_print;
    handler->mhandler.callback = cmd;

    QTAILQ_INSERT_TAIL(list, handler, node);
}

void register_monitor_cmd(const char *name,
                          const char *args_type,
                          const char *params,
                          const char *help,
                          UserPrintHandler *user_print,
                          CommandHandler *cmd)
{
    do_register_cmd(&mon_cmds, name, args_type, params, help, user_print, cmd);
}

void register_monitor_info_cmd(const char *name,
                               const char *help,
                               UserPrintHandler *user_print,
                               InfoHandler *info)
{
    do_register_cmd(&info_cmds, name, "", "", help, user_print, info);
}

void register_monitor_cmd_legacy(const char *name,
                                 const char *args_type,
                                 const char *params,
                                 const char *help,
                                 LegacyCommandHandler *cmd)
{
    do_register_cmd(&mon_cmds, name, args_type, params, help, NULL, cmd);
}

void register_monitor_info_cmd_legacy(const char *name,
                                      const char *help,
                                      LegacyInfoHandler *info)
{
    do_register_cmd(&info_cmds, name, "", "", help, NULL, info);
}

static void monitor_internal_init(void)
{
    register_monitor_cmd_legacy("help|?",
                                "name:s?",
                                "[cmd]",
                                "show the help",
                                do_help_cmd);

    register_monitor_cmd("info",
                         "item:s?",
                         "[subcommand]",
                         "show various information about the system state",
                         monitor_user_noop,
                         do_info);

    register_monitor_info_cmd("commands",
                              "list QMP available commands",
                              monitor_user_noop,
                              do_info_commands);

    register_monitor_cmd("getfd",
                         "fdname:s",
                         "getfd name",
                         "receive a file descriptor via SCM rights and assign it a name",
                         monitor_user_noop,
                         do_getfd);

    register_monitor_cmd("closefd",
                         "fdname:s",
                         "closefd name",
                         "close a file descriptor previously passed via SCM rights",
                         monitor_user_noop,
                         do_closefd);
}

void monitor_subsystem_init(void)
{
    monitor_internal_init();
    monitor_builtin_init();
}


/*******************************************************************/

static const char *pch;
static jmp_buf expr_env;

static void expr_error(Monitor *mon, const char *msg)
{
    monitor_printf(mon, "%s\n", msg);
    longjmp(expr_env, 1);
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
            uint64_t reg=0;

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
            if (ret == -1)
                expr_error(mon, "unknown register");
            else if (ret == -2)
                expr_error(mon, "no cpu defined");
            n = reg;
        }
        break;
    case '\0':
        expr_error(mon, "unexpected end of expression");
        n = 0;
        break;
    default:
        n = strtoull(pch, &p, 0);
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
        return -1;
    }
    while (qemu_isspace(*pch))
        pch++;
    *pval = expr_sum(mon);
    *pp = pch;
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
        return -1;
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

static const MonitorCommandHandler *monitor_find_command(const char *cmdname)
{
    const MonitorCommandHandler *cmd;

    QTAILQ_FOREACH(cmd, &mon_cmds, node) {
        if (compare_cmd(cmdname, cmd->name)) {
            return cmd;
        }
    }

    return NULL;
}

static const MonitorCommandHandler *monitor_parse_command(Monitor *mon,
                                              const char *cmdline,
                                              QDict *qdict)
{
    const char *p, *typestr;
    int c;
    const MonitorCommandHandler *cmd;
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

static void monitor_print_error(Monitor *mon)
{
    qerror_print(mon->error);
    QDECREF(mon->error);
    mon->error = NULL;
}

static void monitor_call_handler(Monitor *mon, const MonitorCommandHandler *cmd,
                                 const QDict *params)
{
    QObject *data = NULL;

    cmd->mhandler.cmd_new(mon, params, &data);

    if (monitor_ctrl_mode(mon)) {
        /* Monitor Protocol */
        monitor_protocol_emitter(mon, data);
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
    const MonitorCommandHandler *cmd;

    qdict = qdict_new();

    cmd = monitor_parse_command(mon, cmdline, qdict);
    if (!cmd)
        goto out;

    qemu_errors_to_mon(mon);

    if (monitor_handler_ported(cmd)) {
        monitor_call_handler(mon, cmd, qdict);
    } else {
        cmd->mhandler.cmd(mon, qdict);
    }

    if (monitor_has_error(mon))
        monitor_print_error(mon);

    qemu_errors_to_previous();

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
    const MonitorCommandHandler *cmd;

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
        QTAILQ_FOREACH(cmd, &mon_cmds, node) {
            cmd_completion(cmdname, cmd->name);
        }
    } else {
        /* find the command */
        QTAILQ_FOREACH(cmd, &mon_cmds, node) {
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
                QTAILQ_FOREACH(cmd, &info_cmds, node) {
                    cmd_completion(str, cmd->name);
                }
/* FIXME */
#if 0
            } else if (!strcmp(cmd->name, "sendkey")) {
                char *sep = strrchr(str, '-');
                if (sep)
                    str = sep + 1;
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(key = key_defs; key->name != NULL; key++) {
                    cmd_completion(str, key->name);
                }
#endif
            } else if (!strcmp(cmd->name, "help|?")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                QTAILQ_FOREACH(cmd, &mon_cmds, node) {
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
        qemu_error_new(QERR_MISSING_PARAMETER, name);
        return -1;
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
                qemu_error_new(QERR_INVALID_PARAMETER_TYPE, name, "string");
                return -1;
            }
            break;
        case '/': {
            int i;
            const char *keys[] = { "count", "format", "size", NULL };

            for (i = 0; keys[i]; i++) {
                QObject *obj = qdict_get(args, keys[i]);
                if (!obj) {
                    qemu_error_new(QERR_MISSING_PARAMETER, name);
                    return -1;
                }
                if (qobject_type(obj) != QTYPE_QINT) {
                    qemu_error_new(QERR_INVALID_PARAMETER_TYPE, name, "int");
                    return -1;
                }
            }
            break;
        }
        case 'i':
        case 'l':
        case 'M':
            if (qobject_type(value) != QTYPE_QINT) {
                qemu_error_new(QERR_INVALID_PARAMETER_TYPE, name, "int");
                return -1;
            }
            break;
        case '-':
            if (qobject_type(value) != QTYPE_QINT &&
                qobject_type(value) != QTYPE_QBOOL) {
                qemu_error_new(QERR_INVALID_PARAMETER_TYPE, name, "bool");
                return -1;
            }
            if (qobject_type(value) == QTYPE_QBOOL) {
                /* handlers expect a QInt, they need to be changed */
                qdict_put(args, name,
                         qint_from_int(qbool_get_int(qobject_to_qbool(value))));
            }
            break;
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

/*
 * This is not trivial, we have to parse Monitor command's argument
 * type syntax to be able to check the arguments provided by clients.
 *
 * In the near future we will be using an array for that and will be
 * able to drop all this parsing...
 */
static int monitor_check_qmp_args(const MonitorCommandHandler *cmd, QDict *args)
{
    int err;
    const char *p;
    CmdArgs cmd_args;

    if (cmd->args_type == NULL) {
        return (qdict_size(args) == 0 ? 0 : -1);
    }

    err = 0;
    cmd_args_init(&cmd_args);

    for (p = cmd->args_type;; p++) {
        if (*p == ':') {
            cmd_args.type = *++p;
            p++;
            if (cmd_args.type == '-') {
                cmd_args.flag = *p++;
                cmd_args.optional = 1;
            } else if (*p == '?') {
                cmd_args.optional = 1;
                p++;
            }

            assert(*p == ',' || *p == '\0');
            err = check_arg(&cmd_args, args);

            QDECREF(cmd_args.name);
            cmd_args_init(&cmd_args);

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

static void handle_qmp_command(JSONMessageParser *parser, QList *tokens)
{
    int err;
    QObject *obj;
    QDict *input, *args;
    const MonitorCommandHandler *cmd;
    Monitor *mon = cur_mon;
    const char *cmd_name, *info_item;

    args = NULL;
    qemu_errors_to_mon(mon);

    obj = json_parser_parse(tokens, NULL);
    if (!obj) {
        // FIXME: should be triggered in json_parser_parse()
        qemu_error_new(QERR_JSON_PARSING);
        goto err_out;
    } else if (qobject_type(obj) != QTYPE_QDICT) {
        qemu_error_new(QERR_QMP_BAD_INPUT_OBJECT, "object");
        qobject_decref(obj);
        goto err_out;
    }

    input = qobject_to_qdict(obj);

    mon->mc->id = qdict_get(input, "id");
    qobject_incref(mon->mc->id);

    obj = qdict_get(input, "execute");
    if (!obj) {
        qemu_error_new(QERR_QMP_BAD_INPUT_OBJECT, "execute");
        goto err_input;
    } else if (qobject_type(obj) != QTYPE_QSTRING) {
        qemu_error_new(QERR_QMP_BAD_INPUT_OBJECT, "string");
        goto err_input;
    }

    cmd_name = qstring_get_str(qobject_to_qstring(obj));

    /*
     * XXX: We need this special case until we get info handlers
     * converted into 'query-' commands
     */
    if (compare_cmd(cmd_name, "info")) {
        qemu_error_new(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_input;
    } else if (strstart(cmd_name, "query-", &info_item)) {
        cmd = monitor_find_command("info");
        qdict_put_obj(input, "arguments",
                      qobject_from_jsonf("{ 'item': %s }", info_item));
    } else {
        cmd = monitor_find_command(cmd_name);
        if (!cmd || !monitor_handler_ported(cmd)) {
            qemu_error_new(QERR_COMMAND_NOT_FOUND, cmd_name);
            goto err_input;
        }
    }

    obj = qdict_get(input, "arguments");
    if (!obj) {
        args = qdict_new();
    } else {
        args = qobject_to_qdict(obj);
        QINCREF(args);
    }

    QDECREF(input);

    err = monitor_check_qmp_args(cmd, args);
    if (err < 0) {
        goto err_out;
    }

    monitor_call_handler(mon, cmd, args);
    goto out;

err_input:
    QDECREF(input);
err_out:
    monitor_protocol_emitter(mon, NULL);
out:
    QDECREF(args);
    qemu_errors_to_previous();
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

/**
 * monitor_control_event(): Print QMP gretting
 */
static void monitor_control_event(void *opaque, int event)
{
    if (event == CHR_EVENT_OPENED) {
        QObject *data;
        Monitor *mon = opaque;

        json_message_parser_init(&mon->mc->parser, handle_qmp_command);

        data = qobject_from_jsonf("{ 'QMP': { 'capabilities': [] } }");
        assert(data != NULL);

        monitor_json_emitter(mon, data);
        qobject_decref(data);
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

void monitor_init(CharDriverState *chr, int flags)
{
    Monitor *mon;

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
    if (!cur_mon || (flags & MONITOR_IS_DEFAULT))
        cur_mon = mon;
}

typedef struct QemuErrorSink QemuErrorSink;
struct QemuErrorSink {
    enum {
        ERR_SINK_FILE,
        ERR_SINK_MONITOR,
    } dest;
    union {
        FILE    *fp;
        Monitor *mon;
    };
    QemuErrorSink *previous;
};

static QemuErrorSink *qemu_error_sink;

void qemu_errors_to_file(FILE *fp)
{
    QemuErrorSink *sink;

    sink = qemu_mallocz(sizeof(*sink));
    sink->dest = ERR_SINK_FILE;
    sink->fp = fp;
    sink->previous = qemu_error_sink;
    qemu_error_sink = sink;
}

void qemu_errors_to_mon(Monitor *mon)
{
    QemuErrorSink *sink;

    sink = qemu_mallocz(sizeof(*sink));
    sink->dest = ERR_SINK_MONITOR;
    sink->mon = mon;
    sink->previous = qemu_error_sink;
    qemu_error_sink = sink;
}

void qemu_errors_to_previous(void)
{
    QemuErrorSink *sink;

    assert(qemu_error_sink != NULL);
    sink = qemu_error_sink;
    qemu_error_sink = sink->previous;
    qemu_free(sink);
}

void qemu_error(const char *fmt, ...)
{
    va_list args;

    assert(qemu_error_sink != NULL);
    switch (qemu_error_sink->dest) {
    case ERR_SINK_FILE:
        va_start(args, fmt);
        vfprintf(qemu_error_sink->fp, fmt, args);
        va_end(args);
        break;
    case ERR_SINK_MONITOR:
        va_start(args, fmt);
        monitor_vprintf(qemu_error_sink->mon, fmt, args);
        va_end(args);
        break;
    }
}

void qemu_error_internal(const char *file, int linenr, const char *func,
                         const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    assert(qemu_error_sink != NULL);

    va_start(va, fmt);
    qerror = qerror_from_info(file, linenr, func, fmt, &va);
    va_end(va);

    switch (qemu_error_sink->dest) {
    case ERR_SINK_FILE:
        qerror_print(qerror);
        QDECREF(qerror);
        break;
    case ERR_SINK_MONITOR:
        assert(qemu_error_sink->mon->error == NULL);
        qemu_error_sink->mon->error = qerror;
        break;
    }
}
