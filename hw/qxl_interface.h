#ifndef _H_QXL_INTERFACE
#define _H_QXL_INTERFACE

typedef unsigned long QXLDevRef;

void qxl_get_info(QXLDevRef dev_ref, QXLDevInfo *info);
int qxl_get_command(QXLDevRef dev_ref, QXLCommand *cmd);
void qxl_release_resource(QXLDevRef dev_ref, QXLReleaseInfo *release_info);
void qxl_notify_update(QXLDevRef dev_ref, uint32_t update_id);
int qxl_req_cmd_notification(QXLDevRef dev_ref);
int qxl_get_cursor_command(QXLDevRef dev_ref, QXLCommand *cmd);
int qxl_req_cursor_notification(QXLDevRef dev_ref);
int qxl_has_command(QXLDevRef dev_ref);
const Rect *qxl_get_update_area(QXLDevRef dev_ref);
int qxl_flush_resources(QXLDevRef dev_ref);
void qxl_set_save_data(QXLDevRef dev_ref, void *data, int size);
void *qxl_get_save_data(QXLDevRef dev_ref);

QXLWorker *qxl_interface_create_worker(QXLDevRef dev, int id);

#endif

