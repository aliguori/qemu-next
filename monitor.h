#ifndef MONITOR_H
#define MONITOR_H

#include "qemu-common.h"
#include "qemu-char.h"
#include "qdict.h"
#include "block.h"

extern Monitor *cur_mon;

/* flags for monitor_init */
#define MONITOR_IS_DEFAULT    0x01
#define MONITOR_USE_READLINE  0x02
#define MONITOR_USE_CONTROL   0x04

/* QMP events */
typedef enum MonitorEvent {
    QEVENT_DEBUG,
    QEVENT_SHUTDOWN,
    QEVENT_RESET,
    QEVENT_POWERDOWN,
    QEVENT_STOP,
    QEVENT_VNC_CONNECTED,
    QEVENT_VNC_INITIALIZED,
    QEVENT_VNC_DISCONNECTED,
    QEVENT_MAX,
} MonitorEvent;

void monitor_protocol_event(MonitorEvent event, QObject *data);
void monitor_init(CharDriverState *chr, int flags);

int monitor_suspend(Monitor *mon);
void monitor_resume(Monitor *mon);

void monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                 BlockDriverCompletionFunc *completion_cb,
                                 void *opaque);

int monitor_get_fd(Monitor *mon, const char *fdname);

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap);
void monitor_printf(Monitor *mon, const char *fmt, ...)
    __attribute__ ((__format__ (__printf__, 2, 3)));
void monitor_print_filename(Monitor *mon, const char *filename);
void monitor_flush(Monitor *mon);

typedef void (UserPrintHandler)(Monitor *, const QObject *);
typedef void (CommandHandler)(Monitor *, const QDict *, QObject **);
typedef void (InfoHandler)(Monitor *, QObject **);

void register_monitor_cmd(const char *name,
                          const char *args_type,
                          const char *params,
                          const char *help,
                          UserPrintHandler *user_print,
                          CommandHandler *cmd);

void register_monitor_info_cmd(const char *name,
                               const char *help,
                               UserPrintHandler *user_print,
                               InfoHandler *info);

/* Legacy only, do not add new commands using these functions */
typedef void (LegacyInfoHandler)(Monitor *mon);
typedef void (LegacyCommandHandler)(Monitor *mon, const QDict *);

void register_monitor_cmd_legacy(const char *name,
                                 const char *args_type,
                                 const char *params,
                                 const char *help,
                                 LegacyCommandHandler *cmd);

void register_monitor_info_cmd_legacy(const char *name,
                                      const char *help,
                                      LegacyInfoHandler *info);
void monitor_subsystem_init(void);

void monitor_builtin_init(void);

int get_monitor_def(uint64_t *pval, const char *name);

int monitor_ctrl_mode(const Monitor *mon);

#endif /* !MONITOR_H */
