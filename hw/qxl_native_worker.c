#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "qemu-common.h"

#include "qemu-spice.h"
#include "spice-display.h"
#include "qxl_interface.h"

#define qxl_error(format, ...) {                                 \
    printf("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__ );   \
    exit(-1);                                                    \
}

#define qxl_printf(format, ...) \
    printf("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__ )

typedef struct QxlDispatcher {
    QXLWorker base;
    int id;
    QXLDevRef dev_ref;
    QXLDevInfo dev_info;
} QxlDispatcher;

static void native_qxl_worker_wakeup(QXLWorker *worker)
{
    QxlDispatcher *dispatcher = DO_UPCAST(QxlDispatcher, base, worker);
    QXLCommand cmd;

    qxl_printf("");

    for (;;) {
        if (qxl_get_command(dispatcher->dev_ref, &cmd)) {
            switch (cmd.type) {
            case QXL_CMD_DRAW: {
                QXLDrawable *draw_cmd = (QXLDrawable *)(cmd.data + dispatcher->dev_info.phys_delta);
                qxl_release_resource(dispatcher->dev_ref, &draw_cmd->release_info);
                break;
            }
            case QXL_CMD_UPDATE: {
                QXLUpdateCmd *update_cmd = (QXLUpdateCmd *)(cmd.data + dispatcher->dev_info.phys_delta);
                qxl_notify_update(dispatcher->dev_ref, update_cmd->update_id);
                qxl_release_resource(dispatcher->dev_ref, &update_cmd->release_info);
                break;
            }
            case QXL_CMD_MESSAGE: {
                QXLMessage *message = (QXLMessage *)(cmd.data + dispatcher->dev_info.phys_delta);
                qxl_printf("MESSAGE: %s", message->data);
                qxl_release_resource(dispatcher->dev_ref, &message->release_info);
                break;
            }
            default:
                qxl_error("bad command type");
            }
            continue;
        }
        if (qxl_req_cmd_notification(dispatcher->dev_ref)) {
            break;
        }
    }
    for (;;) {
        if (qxl_get_cursor_command(dispatcher->dev_ref, &cmd)) {
            switch (cmd.type) {
            case QXL_CMD_CURSOR: {
                QXLCursorCmd *cursor_cmd = (QXLCursorCmd *)(cmd.data + dispatcher->dev_info.phys_delta);
                qxl_release_resource(dispatcher->dev_ref, &cursor_cmd->release_info);
                break;
            }
             default:
                qxl_error("bad command type");
            }
            continue;
        }
        if (qxl_req_cursor_notification(dispatcher->dev_ref)) {
            break;
        }
    }
}

static void native_qxl_worker_attach(QXLWorker *worker)
{
    QxlDispatcher *dispatcher = DO_UPCAST(QxlDispatcher, base, worker);

    qxl_printf("");

    qxl_get_info(dispatcher->dev_ref, &dispatcher->dev_info);
    native_qxl_worker_wakeup(worker);
}

static void native_qxl_worker_detach(QXLWorker *worker)
{
    qxl_printf("");
    native_qxl_worker_wakeup(worker);
}

static void native_qxl_worker_update_area(QXLWorker *worker)
{
    qxl_printf("");
    native_qxl_worker_wakeup(worker);
}

static void native_qxl_worker_oom(QXLWorker *worker)
{
    qxl_printf("");
    native_qxl_worker_wakeup(worker);
}

static void native_qxl_worker_start(QXLWorker *worker)
{
    qxl_printf("");
}

static void native_qxl_worker_stop(QXLWorker *worker)
{
    qxl_printf("");
}

static void native_qxl_worker_save(QXLWorker *worker)
{
    qxl_printf("");
}

static void native_qxl_worker_load(QXLWorker *worker)
{
    qxl_printf("");
}

QXLWorker *qxl_interface_create_worker(QXLDevRef dev_ref, int device_id)
{
    QxlDispatcher *dispatcher;

    dispatcher = qemu_malloc(sizeof(QxlDispatcher));
    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->id = device_id;
    dispatcher->dev_ref = dev_ref;

    dispatcher->base.attach = native_qxl_worker_attach;
    dispatcher->base.detach = native_qxl_worker_detach;
    dispatcher->base.wakeup = native_qxl_worker_wakeup;
    dispatcher->base.oom = native_qxl_worker_oom;
    dispatcher->base.save = native_qxl_worker_save;
    dispatcher->base.load = native_qxl_worker_load;
    dispatcher->base.start = native_qxl_worker_start;
    dispatcher->base.stop = native_qxl_worker_stop;
    dispatcher->base.update_area = native_qxl_worker_update_area;

    return &dispatcher->base;
}

