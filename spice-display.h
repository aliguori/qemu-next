#include <spice/ipc_ring.h>
#include <spice/draw.h>
#include <spice/qxl_dev.h>

#include "pflib.h"

#define NUM_MEMSLOTS 8
#define MEMSLOT_GENERATION_BITS 8
#define MEMSLOT_SLOT_BITS 8

#define MEMSLOT_GROUP_HOST  0
#define MEMSLOT_GROUP_GUEST 1
#define NUM_MEMSLOTS_GROUPS 2

#define NUM_SURFACES 1024

typedef struct SimpleSpiceDisplay {
    DisplayState *ds;
    void *buf;
    int bufsize;
    QXLWorker *worker;
    QXLInstance qxl;
    uint32_t unique;
    QemuPfConv *conv;

    pthread_mutex_t lock;
    SpiceRect dirty;
    int notify;
    int running;
} SimpleSpiceDisplay;

typedef struct SimpleSpiceUpdate {
    QXLDrawable drawable;
    QXLImage image;
    QXLCommandExt ext;
    uint8_t *bitmap;
} SimpleSpiceUpdate;

int qemu_spice_rect_is_empty(const SpiceRect* r);
void qemu_spice_rect_union(SpiceRect *dest, const SpiceRect *r);

SimpleSpiceUpdate *qemu_spice_create_update(SimpleSpiceDisplay *sdpy);
void qemu_spice_destroy_update(SimpleSpiceDisplay *sdpy, SimpleSpiceUpdate *update);
void qemu_spice_create_host_memslot(SimpleSpiceDisplay *ssd);
void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_destroy_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_vm_change_state_handler(void *opaque, int running, int reason);

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h);
void qemu_spice_display_resize(SimpleSpiceDisplay *ssd);
void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd);
