#ifndef QEMU_HMP_H
#define QEMU_HMP_H

#include "qemu-common.h"
#include "monitor.h"

int hmp_quit(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_eject(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_block_passwd(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_change(Monitor *mon, const QDict *qdict, QObject **ret_data);
void hmp_info_version(Monitor *mon, QObject **ret_data);
int hmp_screendump(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_stop(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_cont(Monitor *mon, const QDict *qdict, QObject **ret_data);

#endif
