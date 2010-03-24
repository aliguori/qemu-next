#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "monitor.h"
#include "console.h"
#include "sysemu.h"

#include "spice-display.h"

#define REDHAT_PCI_VENDOR_ID 0x1b36
#define QXL_DEVICE_ID 0x0100 /* 0x100-0x11f reserved for spice */
#define QXL_REVISION 0x01

static struct SpiceDisplay {
    DisplayState *ds;
    void *buf;
    int bufsize;
    QXLWorker *worker;
    Rect dirty;
    int unique;
    int is_attached:1;
    pthread_mutex_t lock;
} sdpy;

QXLUpdate *qemu_spice_display_create_update(DisplayState *ds, Rect *dirty, int unique)
{
    QXLUpdate *update;
    QXLDrawable *drawable;
    QXLImage *image;
    QXLCommand *cmd;

    dirty->left = 0;
#if 0
    fprintf(stderr, "%s: lr %d -> %d,  tb -> %d -> %d\n", __FUNCTION__,
            dirty->left, dirty->right,
            dirty->top, dirty->bottom);
#endif

    update   = qemu_mallocz(sizeof(*update));
    drawable = &update->drawable;
    image    = &update->image;
    cmd      = &update->cmd;

    drawable->bbox            = *dirty;
    drawable->clip.type       = CLIP_TYPE_NONE;
    drawable->clip.data       = 0;
    drawable->effect          = QXL_EFFECT_OPAQUE;
    drawable->release_info.id = (UINT64)update;
    drawable->bitmap_offset   = 0;
    drawable->type            = QXL_DRAW_COPY;

    drawable->u.copy.rop_decriptor   = ROPD_OP_PUT;
    drawable->u.copy.src_bitmap      = (PHYSICAL)image;
    drawable->u.copy.src_area.left   = drawable->u.copy.src_area.top = 0;
    drawable->u.copy.src_area.right  = dirty->right - dirty->left;
    drawable->u.copy.src_area.bottom = dirty->bottom - dirty->top;
    drawable->u.copy.scale_mode = 0;
    memset(&drawable->u.copy.mask, 0, sizeof(QMask));

    image->descriptor.type   = IMAGE_TYPE_BITMAP;
    image->descriptor.flags  = 0;
    QXL_SET_IMAGE_ID(image, QXL_IMAGE_GROUP_DEVICE, unique);
    image->bitmap.flags      = QXL_BITMAP_DIRECT | QXL_BITMAP_TOP_DOWN | QXL_BITMAP_UNSTABLE;
    image->bitmap.stride     = ds_get_linesize(ds);
    image->descriptor.width  = image->bitmap.x = drawable->u.copy.src_area.right;
    image->descriptor.height = image->bitmap.y = drawable->u.copy.src_area.bottom;
    image->bitmap.data = (PHYSICAL)(ds_get_data(ds) +
                                    dirty->top * image->bitmap.stride +
                                    dirty->left * ds_get_bytes_per_pixel(ds));
    image->bitmap.palette = 0;
    switch (ds_get_bits_per_pixel(ds)) {
    case 16:
        image->bitmap.format = BITMAP_FMT_16BIT;
        break;
    case 32:
        image->bitmap.format = BITMAP_FMT_32BIT;
        break;
    default:
        fprintf(stderr, "%s: unhandled depth: %d bits\n", __FUNCTION__,
                ds_get_bits_per_pixel(ds));
        abort();
    }

    cmd->type = QXL_CMD_DRAW;
    cmd->data = (PHYSICAL)drawable;
    return update;
}

static void spice_vm_change_state_handler(void *opaque, int running, int reason)
{
    if (!sdpy.worker) {
        return;
    }

    if (running) {
        sdpy.worker->start(sdpy.worker);
    } else {
        sdpy.worker->stop(sdpy.worker);
    }
}

/* display listener callbacks */

static void spice_display_update(struct DisplayState *ds, int x, int y, int w, int h)
{
    Rect update_area;

    update_area.left = x,
    update_area.right = x + w;
    update_area.top = y;
    update_area.bottom = y + h;
    pthread_mutex_lock(&sdpy.lock);
    rect_union(&sdpy.dirty, &update_area);
    pthread_mutex_unlock(&sdpy.lock);
}

static void spice_display_resize(struct DisplayState *ds)
{
    if (sdpy.is_attached) {
        sdpy.is_attached = 0;
        sdpy.worker->detach(sdpy.worker);
    }

    pthread_mutex_lock(&sdpy.lock);
    sdpy.dirty.left   = 0;
    sdpy.dirty.right  = ds_get_width(ds);
    sdpy.dirty.top    = 0;
    sdpy.dirty.bottom = ds_get_height(ds);
    pthread_mutex_unlock(&sdpy.lock);

    if (!sdpy.is_attached && sdpy.worker) {
        sdpy.is_attached = 1;
        sdpy.worker->attach(sdpy.worker);
    }
    qemu_spice_tablet_size(ds_get_width(ds), ds_get_height(ds));
}

static void spice_display_refresh(struct DisplayState *ds)
{
    vga_hw_update();
    if (rect_is_empty(&sdpy.dirty))
        return;
    if (sdpy.is_attached) {
        sdpy.worker->wakeup(sdpy.worker);
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInterface *qxl, QXLWorker *qxl_worker)
{
    sdpy.worker = qxl_worker;
}

static void interface_set_compression_level(QXLInterface *qxl, int level)
{
    /* nothing to do */
}

static void interface_set_mm_time(QXLInterface *qxl, uint32_t mm_time)
{
    /* nothing to do */
}

static VDObjectRef interface_register_mode_change(QXLInterface *qxl,
                                                  qxl_mode_change_notifier_t notifier,
                                                  void *opaque)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
    return 0;
}

static void interface_unregister_mode_change(QXLInterface *qxl, VDObjectRef notifier)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
}

static void interface_get_info(QXLInterface *qxl, QXLDevInfo *info)
{
    int stride = ds_get_width(sdpy.ds) * 4;
    int size;

#if 0
    fprintf(stderr, "%s: %dx%d @ %d\n", __FUNCTION__,
            ds_get_width(sdpy.ds), ds_get_height(sdpy.ds),
            ds_get_bits_per_pixel(sdpy.ds));
#endif

    info->x_res    = ds_get_width(sdpy.ds);
    info->y_res    = ds_get_height(sdpy.ds);
    info->bits     = 32;

    size = stride * info->y_res;
    if (sdpy.bufsize < size) {
        sdpy.bufsize = size;
        sdpy.buf = qemu_realloc(sdpy.buf, sdpy.bufsize);
    }

    info->ram_size = sdpy.bufsize;
    info->use_hardware_cursor = false;
    info->phys_start = 0;
    info->phys_end = ~info->phys_start;
    info->phys_delta = 0;

    info->draw_area.buf    = sdpy.buf;
    info->draw_area.size   = sdpy.bufsize;
    info->draw_area.line_0 = info->draw_area.buf;
    info->draw_area.stride = stride;
    info->draw_area.width  = info->x_res;
    info->draw_area.heigth = info->y_res;
}

static int interface_get_command(QXLInterface *qxl, struct QXLCommand *cmd)
{
    QXLUpdate *update;

    if (rect_is_empty(&sdpy.dirty))
        return false;
    pthread_mutex_lock(&sdpy.lock);
    update = qemu_spice_display_create_update(sdpy.ds, &sdpy.dirty, ++sdpy.unique);
    memset(&sdpy.dirty, 0, sizeof(sdpy.dirty));
    pthread_mutex_unlock(&sdpy.lock);
    *cmd = update->cmd;
    return true;
}

static int interface_req_cmd_notification(QXLInterface *qxl)
{
    /* nothing to do */
    return 1;
}

static int interface_has_command(QXLInterface *qxl)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
    return 0;
}

static void interface_release_resource(QXLInterface *qxl, union QXLReleaseInfo *release_info)
{
    UINT64 id = release_info->id;
    qemu_free((void *)id);
}

static int interface_get_cursor_command(QXLInterface *qxl, struct QXLCommand *cmd)
{
    /* nothing to do */
    return 0;
}

static int interface_req_cursor_notification(QXLInterface *qxl)
{
    /* nothing to do */
    return 1;
}

static const struct Rect *interface_get_update_area(QXLInterface *qxl)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
    return NULL;
}

static void interface_notify_update(QXLInterface *qxl, uint32_t update_id)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
}

static void interface_set_save_data(QXLInterface *qxl, void *data, int size)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
}

static void *interface_get_save_data(QXLInterface *qxl)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
    return NULL;
}

static int interface_flush_resources(QXLInterface *qxl)
{
    fprintf(stderr, "%s:\n", __FUNCTION__);
    abort();
    return 0;
}

static QXLInterface dpy_interface = {
    .base.base_version = VM_INTERFACE_VERSION,
    .base.type = VD_INTERFACE_QXL,
    .base.description = "display",
    .base.major_version = VD_INTERFACE_QXL_MAJOR,
    .base.minor_version = VD_INTERFACE_QXL_MINOR,

    .pci_vendor = REDHAT_PCI_VENDOR_ID,
    .pci_id = QXL_DEVICE_ID,
    .pci_revision = QXL_REVISION,

    .attache_worker = interface_attach_worker,
    .set_compression_level = interface_set_compression_level,
    .set_mm_time = interface_set_mm_time,
    .register_mode_change = interface_register_mode_change,
    .unregister_mode_change = interface_unregister_mode_change,

    .get_info = interface_get_info,
    .get_command = interface_get_command,
    .req_cmd_notification = interface_req_cmd_notification,
    .has_command = interface_has_command,
    .release_resource = interface_release_resource,
    .get_cursor_command = interface_get_cursor_command,
    .req_cursor_notification = interface_req_cursor_notification,
    .get_update_area = interface_get_update_area,
    .notify_update = interface_notify_update,
    .set_save_data = interface_set_save_data,
    .get_save_data = interface_get_save_data,
    .flush_resources = interface_flush_resources,
};

void qemu_spice_display_init(DisplayState *ds)
{
    DisplayChangeListener *display_listener;

    assert(sdpy.ds == NULL);
    sdpy.ds = ds;
    pthread_mutex_init(&sdpy.lock, NULL);

    display_listener = qemu_mallocz(sizeof(DisplayChangeListener));
    display_listener->dpy_update = spice_display_update;
    display_listener->dpy_resize = spice_display_resize;
    display_listener->dpy_refresh = spice_display_refresh;
    register_displaychangelistener(ds, display_listener);

    qemu_spice_add_interface(&dpy_interface.base);
    qemu_add_vm_change_state_handler(spice_vm_change_state_handler, NULL);
}
