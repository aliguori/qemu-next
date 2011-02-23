/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "sysemu.h"
#include "monitor.h"
#include "qjson.h"
#include "qint.h"
#include "cpu-common.h"
#include "kvm.h"
#include "balloon.h"
#include "trace.h"


static QEMUBalloonEvent *qemu_balloon_event;
void *qemu_balloon_event_opaque;

void qemu_add_balloon_handler(QEMUBalloonEvent *func, void *opaque)
{
    qemu_balloon_event = func;
    qemu_balloon_event_opaque = opaque;
}

static QEMUBalloonStatsFunc *qemu_balloon_stats_func;
static void *qemu_balloon_stats_opaque;

void qemu_add_balloon_stats_handler(QEMUBalloonStatsFunc *func, void *opaque)
{
    qemu_balloon_stats_func = func;
    qemu_balloon_stats_opaque = opaque;
}

int qemu_balloon(ram_addr_t target)
{
    if (qemu_balloon_event) {
        trace_balloon_event(qemu_balloon_event_opaque, target);
        qemu_balloon_event(qemu_balloon_event_opaque, target);
        return 1;
    } else {
        return 0;
    }
}

int qemu_balloon_stats(BalloonInfo *info)
{
    if (qemu_balloon_stats_func) {
        qemu_balloon_stats_func(qemu_balloon_stats_opaque, info);
        return 1;
    } else {
        return 0;
    }
}

static void print_balloon_stat(const char *key, QObject *obj, void *opaque)
{
    Monitor *mon = opaque;

    if (strcmp(key, "actual"))
        monitor_printf(mon, ",%s=%" PRId64, key,
                       qint_get_int(qobject_to_qint(obj)));
}

void monitor_print_balloon(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);
    if (!qdict_haskey(qdict, "actual"))
        return;

    monitor_printf(mon, "balloon: actual=%" PRId64,
                   qdict_get_int(qdict, "actual") >> 20);
    qdict_iter(qdict, print_balloon_stat, mon);
    monitor_printf(mon, "\n");
}

void do_info_balloon(Monitor *mon, QObject **ret_data)
{
    int ret;
    BalloonInfo *info;
    QDict *stats;

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        qerror_report(QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
        return;
    }

    info = qmp_alloc_balloon_info();

    ret = qemu_balloon_stats(info);
    if (ret == 0) {
        qerror_report(QERR_DEVICE_NOT_ACTIVE, "balloon");
        qmp_free_balloon_info(info);
        return;
    }

    stats = qdict_new();
    qdict_put(stats, "actual", qint_from_int(info->actual));
    if (info->has_mem_swapped_in) {
        qdict_put(stats, "mem_swapped_in", qint_from_int(info->mem_swapped_in));
    }
    if (info->has_mem_swapped_out) {
        qdict_put(stats, "mem_swapped_out", qint_from_int(info->mem_swapped_out));
    }
    if (info->has_major_page_faults) {
        qdict_put(stats, "major_page_faults", qint_from_int(info->major_page_faults));
    }
    if (info->has_minor_page_faults) {
        qdict_put(stats, "minor_page_faults", qint_from_int(info->minor_page_faults));
    }
    if (info->has_free_mem) {
        qdict_put(stats, "free_mem", qint_from_int(info->free_mem));
    }
    if (info->has_total_mem) {
        qdict_put(stats, "total_mem", qint_from_int(info->total_mem));
    }

    *ret_data = QOBJECT(stats);
}

BalloonInfo *qmp_query_balloon(Error **errp)
{
    BalloonInfo *info;

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        qerror_report(QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
        return NULL;
    }

    info = qmp_alloc_balloon_info();

    if (qemu_balloon_stats(info) == 0) {
        qerror_report(QERR_DEVICE_NOT_ACTIVE, "balloon");
        qmp_free_balloon_info(info);
        return NULL;
    }

    return info;
}

/**
 * do_balloon(): Request VM to change its memory allocation
 */
int do_balloon(Monitor *mon, const QDict *params, QObject **ret_data)
{
    int ret;

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        qerror_report(QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
        return -1;
    }

    ret = qemu_balloon(qdict_get_int(params, "value"));
    if (ret == 0) {
        qerror_report(QERR_DEVICE_NOT_ACTIVE, "balloon");
        return -1;
    }

    return 0;
}

void qmp_balloon(int64_t value, Error **errp)
{
    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        error_set(errp, QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
    } else if (qemu_balloon(value) == 0) {
        error_set(errp, QERR_DEVICE_NOT_ACTIVE, "balloon");
    }
}

