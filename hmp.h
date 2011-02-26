#ifndef QEMU_HMP_H
#define QEMU_HMP_H

#include "qemu-common.h"
#include "monitor.h"

int hmp_quit(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_eject(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_block_passwd(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_change(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_screendump(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_stop(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_cont(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_system_reset(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_system_powerdown(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_set_link(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_set_password(Monitor *mon, const QDict *qdict, QObject **ret_data);
int hmp_expire_password(Monitor *mon, const QDict *qdict, QObject **ret_data);
void hmp_info_version(Monitor *mon);
void hmp_info_status(Monitor *mon);
void hmp_info_block(Monitor *mon);
void hmp_info_blockstats(Monitor *mon);
void hmp_info_vnc(Monitor *mon);
void hmp_info_name(Monitor *mon);
void hmp_info_uuid(Monitor *mon);
void hmp_info_cpus(Monitor *mon);
void hmp_info_kvm(Monitor *mon);

#endif
