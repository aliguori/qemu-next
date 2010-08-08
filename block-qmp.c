/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "config-host.h"
#include "qemu-common.h"
#include "qemu-objects.h"
#include "block.h"
#include "block_int.h"
#include "monitor.h"

void bdrv_mon_event(const BlockDriverState *bdrv,
                    BlockMonEventAction action, int is_read)
{
    QObject *data;
    const char *action_str;

    switch (action) {
    case BDRV_ACTION_REPORT:
        action_str = "report";
        break;
    case BDRV_ACTION_IGNORE:
        action_str = "ignore";
        break;
    case BDRV_ACTION_STOP:
        action_str = "stop";
        break;
    default:
        abort();
    }

    data = qobject_from_jsonf("{ 'device': %s, 'action': %s, 'operation': %s }",
                              bdrv->device_name,
                              action_str,
                              is_read ? "read" : "write");
    monitor_protocol_event(QEVENT_BLOCK_IO_ERROR, data);

    qobject_decref(data);
}

static void bdrv_print_dict(QObject *obj, void *opaque)
{
    QDict *bs_dict;
    Monitor *mon = opaque;

    bs_dict = qobject_to_qdict(obj);

    monitor_printf(mon, "%s: type=%s removable=%d",
                        qdict_get_str(bs_dict, "device"),
                        qdict_get_str(bs_dict, "type"),
                        qdict_get_bool(bs_dict, "removable"));

    if (qdict_get_bool(bs_dict, "removable")) {
        monitor_printf(mon, " locked=%d", qdict_get_bool(bs_dict, "locked"));
    }

    if (qdict_haskey(bs_dict, "inserted")) {
        QDict *qdict = qobject_to_qdict(qdict_get(bs_dict, "inserted"));

        monitor_printf(mon, " file=");
        monitor_print_filename(mon, qdict_get_str(qdict, "file"));
        if (qdict_haskey(qdict, "backing_file")) {
            monitor_printf(mon, " backing_file=");
            monitor_print_filename(mon, qdict_get_str(qdict, "backing_file"));
        }
        monitor_printf(mon, " ro=%d drv=%s encrypted=%d",
                            qdict_get_bool(qdict, "ro"),
                            qdict_get_str(qdict, "drv"),
                            qdict_get_bool(qdict, "encrypted"));
    } else {
        monitor_printf(mon, " [not inserted]");
    }

    monitor_printf(mon, "\n");
}

void bdrv_info_print(Monitor *mon, const QObject *data)
{
    qlist_iter(qobject_to_qlist(data), bdrv_print_dict, mon);
}

static void bdrv_info_it(void *opaque, BlockDriverState *bs)
{
    QList *bs_list = opaque;
    const char *type = "unknown";
    QObject *bs_obj;

    switch(bs->type) {
    case BDRV_TYPE_HD:
        type = "hd";
        break;
    case BDRV_TYPE_CDROM:
        type = "cdrom";
        break;
    case BDRV_TYPE_FLOPPY:
        type = "floppy";
        break;
    }

    bs_obj = qobject_from_jsonf("{ 'device': %s, 'type': %s, "
                                "'removable': %i, 'locked': %i }",
                                bs->device_name, type, bs->removable,
                                bs->locked);

    if (bs->drv) {
        QObject *obj;
        QDict *bs_dict = qobject_to_qdict(bs_obj);

        obj = qobject_from_jsonf("{ 'file': %s, 'ro': %i, 'drv': %s, "
                                 "'encrypted': %i }",
                                 bs->filename, bs->read_only,
                                 bs->drv->format_name,
                                 bdrv_is_encrypted(bs));
        if (bs->backing_file[0] != '\0') {
            QDict *qdict = qobject_to_qdict(obj);
            qdict_put(qdict, "backing_file",
                      qstring_from_str(bs->backing_file));
        }

        qdict_put_obj(bs_dict, "inserted", obj);
    }

    qlist_append_obj(bs_list, bs_obj);
}

void bdrv_info(Monitor *mon, QObject **ret_data)
{
    QList *bs_list = qlist_new();
    bdrv_iterate(bdrv_info_it, bs_list);
    *ret_data = QOBJECT(bs_list);
}

static void bdrv_stats_iter(QObject *data, void *opaque)
{
    QDict *qdict;
    Monitor *mon = opaque;

    qdict = qobject_to_qdict(data);
    monitor_printf(mon, "%s:", qdict_get_str(qdict, "device"));

    qdict = qobject_to_qdict(qdict_get(qdict, "stats"));
    monitor_printf(mon, " rd_bytes=%" PRId64
                        " wr_bytes=%" PRId64
                        " rd_operations=%" PRId64
                        " wr_operations=%" PRId64
                        "\n",
                        qdict_get_int(qdict, "rd_bytes"),
                        qdict_get_int(qdict, "wr_bytes"),
                        qdict_get_int(qdict, "rd_operations"),
                        qdict_get_int(qdict, "wr_operations"));
}

void bdrv_stats_print(Monitor *mon, const QObject *data)
{
    qlist_iter(qobject_to_qlist(data), bdrv_stats_iter, mon);
}

static QObject* bdrv_info_stats_bs(BlockDriverState *bs)
{
    QObject *res;
    QDict *dict;

    res = qobject_from_jsonf("{ 'stats': {"
                             "'rd_bytes': %" PRId64 ","
                             "'wr_bytes': %" PRId64 ","
                             "'rd_operations': %" PRId64 ","
                             "'wr_operations': %" PRId64 ","
                             "'wr_highest_offset': %" PRId64
                             "} }",
                             bs->rd_bytes, bs->wr_bytes,
                             bs->rd_ops, bs->wr_ops,
                             bs->wr_highest_sector *
                             (uint64_t)BDRV_SECTOR_SIZE);
    dict  = qobject_to_qdict(res);

    if (*bs->device_name) {
        qdict_put(dict, "device", qstring_from_str(bs->device_name));
    }

    if (bs->file) {
        QObject *parent = bdrv_info_stats_bs(bs->file);
        qdict_put_obj(dict, "parent", parent);
    }

    return res;
}

static void bdrv_info_stats_it(void *opaque, BlockDriverState *bs)
{
    QList *devices = opaque;
    QObject *obj;

    obj = bdrv_info_stats_bs(bs);
    qlist_append_obj(devices, obj);
}

void bdrv_info_stats(Monitor *mon, QObject **ret_data)
{
    QList *devices = qlist_new();

    bdrv_iterate(bdrv_info_stats_it, devices);
    *ret_data = QOBJECT(devices);
}

