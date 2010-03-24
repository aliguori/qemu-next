#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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

static int debug = 1;

int qemu_spice_rect_is_empty(const SpiceRect* r)
{
    return r->top == r->bottom || r->left == r->right;
}

void qemu_spice_rect_union(SpiceRect *dest, const SpiceRect *r)
{
    if (qemu_spice_rect_is_empty(r)) {
        return;
    }

    if (qemu_spice_rect_is_empty(dest)) {
        *dest = *r;
        return;
    }

    dest->top = MIN(dest->top, r->top);
    dest->left = MIN(dest->left, r->left);
    dest->bottom = MAX(dest->bottom, r->bottom);
    dest->right = MAX(dest->right, r->right);
}

SimpleSpiceUpdate *qemu_spice_create_update(SimpleSpiceDisplay *ssd)
{
    SimpleSpiceUpdate *update;
    QXLDrawable *drawable;
    QXLImage *image;
    QXLCommand *cmd;
    uint8_t *src, *dst;
    int by, bw, bh;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        return NULL;
    };

    pthread_mutex_lock(&ssd->lock);
    if (debug > 1)
        fprintf(stderr, "%s: lr %d -> %d,  tb -> %d -> %d\n", __FUNCTION__,
                ssd->dirty.left, ssd->dirty.right,
                ssd->dirty.top, ssd->dirty.bottom);

    update   = qemu_mallocz(sizeof(*update));
    drawable = &update->drawable;
    image    = &update->image;
    cmd      = &update->ext.cmd;

    bw       = ssd->dirty.right - ssd->dirty.left;
    bh       = ssd->dirty.bottom - ssd->dirty.top;
    update->bitmap = qemu_malloc(bw * bh * 4);

    drawable->bbox            = ssd->dirty;
    drawable->clip.type       = SPICE_CLIP_TYPE_NONE;
    drawable->effect          = QXL_EFFECT_OPAQUE;
    drawable->release_info.id = (intptr_t)update;
    drawable->type            = QXL_DRAW_COPY;

    drawable->u.copy.rop_decriptor   = SPICE_ROPD_OP_PUT;
    drawable->u.copy.src_bitmap      = (intptr_t)image;
    drawable->u.copy.src_area.right  = bw;
    drawable->u.copy.src_area.bottom = bh;

    QXL_SET_IMAGE_ID(image, QXL_IMAGE_GROUP_DEVICE, ssd->unique++);
    image->descriptor.type   = SPICE_IMAGE_TYPE_BITMAP;
    image->bitmap.flags      = QXL_BITMAP_DIRECT | QXL_BITMAP_TOP_DOWN;
    image->bitmap.stride     = bw * 4;
    image->descriptor.width  = image->bitmap.x = bw;
    image->descriptor.height = image->bitmap.y = bh;
    image->bitmap.data = (intptr_t)(update->bitmap);
    image->bitmap.palette = 0;
    image->bitmap.format = SPICE_BITMAP_FMT_32BIT;

    if (ssd->conv == NULL) {
        PixelFormat dst = qemu_default_pixelformat(32);
        ssd->conv = qemu_pf_conv_get(&dst, &ssd->ds->surface->pf);
        assert(ssd->conv);
    }

    src = ds_get_data(ssd->ds) +
        ssd->dirty.top * ds_get_linesize(ssd->ds) +
        ssd->dirty.left * ds_get_bytes_per_pixel(ssd->ds);
    dst = update->bitmap;
    for (by = 0; by < bh; by++) {
        qemu_pf_conv_run(ssd->conv, dst, src, bw);
        src += ds_get_linesize(ssd->ds);
        dst += image->bitmap.stride;
    }

    cmd->type = QXL_CMD_DRAW;
    cmd->data = (intptr_t)drawable;

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    pthread_mutex_unlock(&ssd->lock);
    return update;
}

void qemu_spice_destroy_update(SimpleSpiceDisplay *sdpy, SimpleSpiceUpdate *update)
{
    qemu_free(update->bitmap);
    qemu_free(update);
}

void qemu_spice_create_host_memslot(SimpleSpiceDisplay *ssd)
{
    QXLDevMemSlot memslot;

    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);

    memset(&memslot, 0, sizeof(memslot));
    memslot.slot_group_id = MEMSLOT_GROUP_HOST;
    memslot.virt_end = ~0;
    ssd->worker->add_memslot(ssd->worker, &memslot);
}

void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd)
{
    QXLDevSurfaceCreate surface;

    if (debug)
        fprintf(stderr, "%s: %dx%d\n", __FUNCTION__,
                ds_get_width(ssd->ds), ds_get_height(ssd->ds));

    surface.format     = SPICE_SURFACE_FMT_32_xRGB;
    surface.width      = ds_get_width(ssd->ds);
    surface.height     = ds_get_height(ssd->ds);
    surface.stride     = -surface.width * 4;
    surface.mouse_mode = 0;
    surface.flags      = 0;
    surface.type       = 0;
    surface.mem        = (intptr_t)ssd->buf;
    surface.group_id   = MEMSLOT_GROUP_HOST;
    ssd->worker->create_primary_surface(ssd->worker, 0, &surface);
}

void qemu_spice_destroy_host_primary(SimpleSpiceDisplay *ssd)
{
    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);

    ssd->worker->destroy_primary_surface(ssd->worker, 0);
}

void qemu_spice_vm_change_state_handler(void *opaque, int running, int reason)
{
    SimpleSpiceDisplay *ssd = opaque;

    if (running) {
        ssd->worker->start(ssd->worker);
    } else {
        ssd->worker->stop(ssd->worker);
    }
    ssd->running = running;
}

/* display listener callbacks */

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h)
{
    SpiceRect update_area;

    if (debug > 1)
        fprintf(stderr, "%s: x %d y %d w %d h %d\n", __FUNCTION__, x, y, w, h);
    update_area.left = x,
    update_area.right = x + w;
    update_area.top = y;
    update_area.bottom = y + h;

    pthread_mutex_lock(&ssd->lock);
    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        ssd->notify++;
    }
    qemu_spice_rect_union(&ssd->dirty, &update_area);
    pthread_mutex_unlock(&ssd->lock);
}

void qemu_spice_display_resize(SimpleSpiceDisplay *ssd)
{
    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);

    pthread_mutex_lock(&ssd->lock);
    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    pthread_mutex_unlock(&ssd->lock);

    qemu_spice_destroy_host_primary(ssd);
    qemu_spice_create_host_primary(ssd);
    qemu_pf_conv_put(ssd->conv);
    ssd->conv = NULL;

    pthread_mutex_lock(&ssd->lock);
    ssd->dirty.left   = 0;
    ssd->dirty.right  = ds_get_width(ssd->ds);
    ssd->dirty.top    = 0;
    ssd->dirty.bottom = ds_get_height(ssd->ds);
    ssd->notify++;
    pthread_mutex_unlock(&ssd->lock);
}

void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd)
{
    if (debug > 2)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    vga_hw_update();
    if (ssd->notify) {
        ssd->notify = 0;
        ssd->worker->wakeup(ssd->worker);
        if (debug > 1)
            fprintf(stderr, "%s: notify\n", __FUNCTION__);
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    ssd->worker = qxl_worker;
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    /* nothing to do */
}

static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    if (debug > 2)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    /* nothing to do */
}

static void interface_get_init_info(QXLInstance *sin, QXLDevInitInfo *info)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    info->memslot_gen_bits = MEMSLOT_GENERATION_BITS;
    info->memslot_id_bits  = MEMSLOT_SLOT_BITS;
    info->num_memslots = NUM_MEMSLOTS;
    info->num_memslots_groups = NUM_MEMSLOTS_GROUPS;
    info->internal_groupslot_id = 0;
    info->qxl_ram_size = ssd->bufsize;
    info->n_surfaces = NUM_SURFACES;
}

static int interface_get_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    SimpleSpiceUpdate *update;

    if (debug > 2)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    update = qemu_spice_create_update(ssd);
    if (update == NULL) {
        return false;
    }
    *ext = update->ext;
    return true;
}

static int interface_req_cmd_notification(QXLInstance *sin)
{
    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    return 1;
}

static int interface_has_command(QXLInstance *sin)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    return !qemu_spice_rect_is_empty(&ssd->dirty);
}

static void interface_release_resource(QXLInstance *sin,
                                       struct QXLReleaseInfoExt ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    uintptr_t id;

    if (debug > 1)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    id = ext.info->id;
    qemu_spice_destroy_update(ssd, (void*)id);
}

static int interface_get_cursor_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    if (debug > 2)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    return false;
}

static int interface_req_cursor_notification(QXLInstance *sin)
{
    if (debug)
        fprintf(stderr, "%s:\n", __FUNCTION__);
    return 1;
}

static void interface_notify_update(QXLInstance *sin, uint32_t update_id)
{
    fprintf(stderr, "%s: abort()\n", __FUNCTION__);
    abort();
}

static int interface_flush_resources(QXLInstance *sin)
{
    fprintf(stderr, "%s: abort()\n", __FUNCTION__);
    abort();
    return 0;
}

static const QXLInterface dpy_interface = {
    .base.type               = SPICE_INTERFACE_QXL,
    .base.description        = "qemu simple display",
    .base.major_version      = SPICE_INTERFACE_QXL_MAJOR,
    .base.minor_version      = SPICE_INTERFACE_QXL_MINOR,

    .pci_vendor              = REDHAT_PCI_VENDOR_ID,
    .pci_id                  = QXL_DEVICE_ID,
    .pci_revision            = QXL_REVISION,

    .attache_worker          = interface_attach_worker,
    .set_compression_level   = interface_set_compression_level,
    .set_mm_time             = interface_set_mm_time,

    .get_init_info           = interface_get_init_info,
    .get_command             = interface_get_command,
    .req_cmd_notification    = interface_req_cmd_notification,
    .has_command             = interface_has_command,
    .release_resource        = interface_release_resource,
    .get_cursor_command      = interface_get_cursor_command,
    .req_cursor_notification = interface_req_cursor_notification,
    .notify_update           = interface_notify_update,
    .flush_resources         = interface_flush_resources,
};

static SimpleSpiceDisplay sdpy;

static void display_update(struct DisplayState *ds, int x, int y, int w, int h)
{
    qemu_spice_display_update(&sdpy, x, y, w, h);
}

static void display_resize(struct DisplayState *ds)
{
    qemu_spice_display_resize(&sdpy);
}

static void display_refresh(struct DisplayState *ds)
{
    qemu_spice_display_refresh(&sdpy);
}

static DisplayChangeListener display_listener = {
    .dpy_update  = display_update,
    .dpy_resize  = display_resize,
    .dpy_refresh = display_refresh,
};

void qemu_spice_display_init(DisplayState *ds)
{
    assert(sdpy.ds == NULL);
    sdpy.ds = ds;
    sdpy.bufsize = (16 * 1024 * 1024);
    sdpy.buf = qemu_malloc(sdpy.bufsize);
    pthread_mutex_init(&sdpy.lock, NULL);
    register_displaychangelistener(ds, &display_listener);

    sdpy.qxl.base.sif = &dpy_interface.base;
    spice_server_add_interface(spice_server, &sdpy.qxl.base);
    assert(sdpy.worker);

    qemu_add_vm_change_state_handler(qemu_spice_vm_change_state_handler, &sdpy);
    qemu_spice_create_host_memslot(&sdpy);
    qemu_spice_create_host_primary(&sdpy);
}
