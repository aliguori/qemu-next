#ifndef QEMU_HMP_H
#define QEMU_HMP_H

#include "qemu-common.h"
#include "qemu-timer.h"
#include "monitor.h"

void hmp_quit(Monitor *mon, const QDict *qdict);
void hmp_eject(Monitor *mon, const QDict *qdict);
void hmp_block_passwd(Monitor *mon, const QDict *qdict);
void hmp_change(Monitor *mon, const QDict *qdict);
void hmp_screendump(Monitor *mon, const QDict *qdict);
void hmp_stop(Monitor *mon, const QDict *qdict);
void hmp_cont(Monitor *mon, const QDict *qdict);
void hmp_system_reset(Monitor *mon, const QDict *qdict);
void hmp_system_powerdown(Monitor *mon, const QDict *qdict);
void hmp_set_link(Monitor *mon, const QDict *qdict);
void hmp_set_password(Monitor *mon, const QDict *qdict);
void hmp_expire_password(Monitor *mon, const QDict *qdict);
void hmp_cpu(Monitor *mon, const QDict *qdict);
void hmp_memsave(Monitor *mon, const QDict *qdict);
void hmp_pmemsave(Monitor *mon, const QDict *qdict);
void hmp_balloon(Monitor *mon, const QDict *qdict);
void hmp_device_add(Monitor *mon, const QDict *qdict);
void hmp_device_del(Monitor *mon, const QDict *qdict);
void hmp_netdev_add(Monitor *mon, const QDict *qdict);
void hmp_netdev_del(Monitor *mon, const QDict *qdict);
void hmp_migrate(Monitor *mon, const QDict *qdict);
void hmp_migrate_cancel(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_speed(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_downtime(Monitor *mon, const QDict *qdict);
void hmp_getfd(Monitor *mon, const QDict *qdict);
void hmp_closefd(Monitor *mon, const QDict *qdict);

void hmp_info_version(Monitor *mon);
void hmp_info_status(Monitor *mon);
void hmp_info_block(Monitor *mon);
void hmp_info_blockstats(Monitor *mon);
void hmp_info_vnc(Monitor *mon);
void hmp_info_name(Monitor *mon);
void hmp_info_uuid(Monitor *mon);
void hmp_info_cpus(Monitor *mon);
void hmp_info_kvm(Monitor *mon);
void hmp_info_chardev(Monitor *mon);
void hmp_info_mice(Monitor *mon);
void hmp_info_pci(Monitor *mon);
void hmp_info_balloon(Monitor *mon);
void hmp_info_migrate(Monitor *mon);
void hmp_info_spice(Monitor *mon);

#endif
