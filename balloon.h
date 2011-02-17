/*
 * Balloon
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_BALLOON_H
#define _QEMU_BALLOON_H

#include "monitor.h"

typedef void (QEMUBalloonEvent)(void *opaque, ram_addr_t target);
void qemu_add_balloon_handler(QEMUBalloonEvent *func, void *opaque);

typedef void (QEMUBalloonStatsFunc)(void *opaque, BalloonInfo *info);

void qemu_add_balloon_stats_handler(QEMUBalloonStatsFunc *func, void *opaque);

int qemu_balloon(ram_addr_t target);
int qemu_balloon_stats(BalloonInfo *info);

void monitor_print_balloon(Monitor *mon, const QObject *data);
void do_info_balloon(Monitor *mon, QObject **ret_data);
int do_balloon(Monitor *mon, const QDict *params, QObject **ret_data);

#endif
