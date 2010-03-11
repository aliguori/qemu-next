#include <pthread.h>
#include <signal.h>

#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "console.h"
#include "hw/vga_int.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "console.h"
#include "pci.h"
#include "hw.h"
#include "loader.h"
#include "cpu-common.h"
#include "kvm.h"

#include "qemu-spice.h"
#include "spice-display.h"
#include "qxl_interface.h"

#define QXL_VRAM_SIZE 4096
#define QXL_DEFAULT_COMPRESSION_LEVEL 0
#define QXL_SAVE_VERSION 20
#define VDI_PORT_SAVE_VERSION 20

#undef ALIGN
#define ALIGN(a, b) (((a) + ((b) - 1)) & ~((b) - 1))

#define QXL_DEV_NAME "qxl"
#define VDI_PORT_DEV_NAME "vdi_port"

enum {
    QXL_MODE_UNDEFINED,
    QXL_MODE_VGA,
    QXL_MODE_NATIVE,
};

typedef struct PCIQXLDevice PCIQXLDevice;

struct PCIQXLDevice {
    PCIDevice pci_dev;
    VGACommonState vga;
    int id;
    QTAILQ_ENTRY(PCIQXLDevice) next;
    int pipe_fd[2];
    int running;
    uint32_t mode;

    Rect dirty_rect;
    uint32_t bits_unique;

    uint32_t io_base;
    QXLRom *rom;
    QXLRom shadow_rom;
    QXLModes *modes;
    uint64_t rom_offset;
    uint32_t rom_size;

    uint8_t *ram_start;
    QXLRam *ram;
    uint64_t ram_phys_addr;

    uint8_t *vram;
    unsigned long vram_offset;
    uint32_t vram_size;

    uint32_t num_free_res;
    QXLReleaseInfo *last_release;
    uint32_t last_release_offset;

    void *worker_data;
    int32_t worker_data_size;

    QXLWorker* worker;
};

static int qxl_debug;
static QTAILQ_HEAD(, PCIQXLDevice) devs = QTAILQ_HEAD_INITIALIZER(devs);
static pthread_t main_thread;
static pthread_mutex_t dirty_lock = PTHREAD_MUTEX_INITIALIZER;

#define dprintf(level, fmt, ...)                                         \
    do { if (qxl_debug >= level) fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)

#define PIXEL_SIZE 0.2936875 //1280x1024 is 14.8" x 11.9"

#define QXL_MODE(x_res, y_res, bits, orientation) \
    {0, x_res, y_res, bits, (x_res) * (bits) / 8, \
     PIXEL_SIZE * (x_res), PIXEL_SIZE * (y_res), orientation}

#define QXL_MODE_16_32(x_res, y_res, orientation) \
    QXL_MODE(x_res, y_res, 16, orientation), QXL_MODE(x_res, y_res, 32, orientation)

#define QXL_MODE_EX(x_res, y_res) \
    QXL_MODE_16_32(x_res, y_res, 0), QXL_MODE_16_32(y_res, x_res, 1), \
    QXL_MODE_16_32(x_res, y_res, 2), QXL_MODE_16_32(y_res, x_res, 3)

//#define QXL_HIRES_MODES

QXLMode qxl_modes[] = {
    QXL_MODE_EX(640, 480),
    QXL_MODE_EX(800, 600),
    QXL_MODE_EX(832, 624),
    QXL_MODE_EX(1024, 576),
    QXL_MODE_EX(1024, 600),
    QXL_MODE_EX(1024, 768),
    QXL_MODE_EX(1152, 864),
    QXL_MODE_EX(1152, 870),
    QXL_MODE_EX(1280, 720),
    QXL_MODE_EX(1280, 768),
    QXL_MODE_EX(1280, 800),
    QXL_MODE_EX(1280, 960),
    QXL_MODE_EX(1280, 1024),
    QXL_MODE_EX(1360, 768),
    QXL_MODE_EX(1366, 768),
    QXL_MODE_EX(1400, 1050),
    QXL_MODE_EX(1440, 900),
    QXL_MODE_EX(1600, 900),
    QXL_MODE_EX(1600, 1200),
    QXL_MODE_EX(1680, 1050),
    QXL_MODE_EX(1920, 1080),
#ifdef QXL_HIRES_MODES
    QXL_MODE_EX(1920, 1200),
    QXL_MODE_EX(1920, 1440),
    QXL_MODE_EX(2048, 1536),
    QXL_MODE_EX(2560, 1600),
    QXL_MODE_EX(2560, 2048),
    QXL_MODE_EX(2800, 2100),
    QXL_MODE_EX(3200, 2400),
#endif
};

typedef struct QXLVga {
    struct DisplayState *ds;
    int active_clients;
    QEMUTimer *timer;
    int need_update;
} QXLVga;

static void qxl_exit_vga_mode(PCIQXLDevice *d);
static void qxl_reset_state(PCIQXLDevice *d);

static QXLVga qxl_vga;

inline uint32_t msb_mask(uint32_t val);

static inline void atomic_or(uint32_t *var, uint32_t add)
{
   __asm__ __volatile__ ("lock; orl %1, %0" : "+m" (*var) : "r" (add) : "memory");
}

static inline uint32_t atomic_exchange(uint32_t val, uint32_t *ptr)
{
   __asm__ __volatile__("xchgl %0, %1" : "+q"(val), "+m" (*ptr) : : "memory");
   return val;
}

static void qxl_init_modes(void)
{
    int i;

    for (i = 0; i < sizeof(qxl_modes) / sizeof(QXLMode); i++) {
        qxl_modes[i].id = i;
    }
}

static UINT32 qxl_max_res_area(void)
{
    UINT32 area = 0;
    int i;

    for (i = 0; i < sizeof(qxl_modes) / sizeof(QXLMode); i++) {
        area = MAX(qxl_modes[i].x_res*qxl_modes[i].y_res, area);
    }
    return area;
}

static void set_dirty(void *base, ram_addr_t offset, void *start, uint32_t length)
{
    assert(start >= base);

    ram_addr_t addr =  (ram_addr_t)((uint8_t*)start - (uint8_t*)base) + offset;
    ram_addr_t end =  ALIGN(addr + length, TARGET_PAGE_SIZE);

    do {
        cpu_physical_memory_set_dirty(addr);
        addr += TARGET_PAGE_SIZE;
    } while ( addr < end );
}

static inline void qxl_rom_set_dirty(PCIQXLDevice *d, void *start, uint32_t length)
{
    set_dirty(d->rom, d->rom_offset, start, length);
}

static int irq_level(PCIQXLDevice *d)
{
    return !!(d->ram->int_pending & d->ram->int_mask);
}

static void qxl_update_irq(void)
{
    PCIQXLDevice *d;

    QTAILQ_FOREACH(d, &devs, next) {
        qemu_set_irq(d->pci_dev.irq[0], irq_level(d));
    }
}

static void qxl_send_events(PCIQXLDevice *d, uint32_t events)
{
    assert(d->running);
    mb();
    if ((d->ram->int_pending & events) == events) {
        return;
    }
    atomic_or(&d->ram->int_pending, events);
    if (pthread_self() == main_thread) {
        qemu_set_irq(d->pci_dev.irq[0], irq_level(d));
    } else {
        //dummy write in order to wake up the main thread
        //to update the irq line
        if (write(d->pipe_fd[1], d, 1) != 1) {
            dprintf(1, "%s: write to pipe failed\n", __FUNCTION__);
        }
    }
}

static void set_draw_area(PCIQXLDevice *d, QXLDevInfo *info)
{
    int stride = info->x_res * sizeof(uint32_t);
    info->draw_area.buf = (uint8_t *)d->ram_start + d->shadow_rom.draw_area_offset;
    info->draw_area.size = stride * info->y_res;
    info->draw_area.line_0 = info->draw_area.buf + info->draw_area.size - stride;
    info->draw_area.stride = -stride;
    info->draw_area.width = info->x_res;
    info->draw_area.heigth = info->y_res;
}

static void _qxl_get_info(PCIQXLDevice *d, QXLDevInfo *info)
{
    QXLMode *mode;

    info->ram_size = d->shadow_rom.num_io_pages << TARGET_PAGE_BITS;

    if (d->mode == QXL_MODE_VGA) {

        info->x_res = ds_get_width(qxl_vga.ds);
        info->y_res = ds_get_height(qxl_vga.ds);
        info->bits = ds_get_bits_per_pixel(qxl_vga.ds);
        if (info->bits != 32) {
            dprintf(1, "%s: unexpected depth %d\n", __FUNCTION__, info->bits);
            abort();
        }

        info->use_hardware_cursor = false;

        info->phys_start = 0;
        info->phys_end = ~info->phys_start;
        info->phys_delta = 0;
        set_draw_area(d, info);
        return;
    }

    mode = &qxl_modes[d->shadow_rom.mode];

    info->x_res = mode->x_res;
    info->y_res = mode->y_res;
    info->bits = mode->bits;
    info->use_hardware_cursor = true;

    info->phys_start = (unsigned long)d->ram_start + d->shadow_rom.pages_offset;
    info->phys_end = (unsigned long)d->ram_start + d->vga.vram_size;
    info->phys_delta = (long)d->ram_start - d->ram_phys_addr;
    set_draw_area(d, info);
}

static int _qxl_get_command(PCIQXLDevice *d, QXLCommand *cmd)
{
    QXLCommandRing *ring;
    QXLUpdate *update;
    int notify;

    if (d->mode == QXL_MODE_VGA) {
        if (rect_is_empty(&d->dirty_rect)) {
            return false;
        }
        pthread_mutex_lock(&dirty_lock);
        update = qemu_spice_display_create_update(qxl_vga.ds, &d->dirty_rect,
                                                  ++d->bits_unique);
        memset(&d->dirty_rect, 0, sizeof(d->dirty_rect));
        pthread_mutex_unlock(&dirty_lock);
        *cmd = update->cmd;
        return true;
    }

    ring = &d->ram->cmd_ring;
    if (RING_IS_EMPTY(ring)) {
        return false;
    }
    *cmd = *RING_CONS_ITEM(ring);
    RING_POP(ring, notify);
    if (notify) {
        qxl_send_events(d, QXL_INTERRUPT_DISPLAY);
    }
    return true;
}

static int _qxl_has_command(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_VGA) {
        return !rect_is_empty(&d->dirty_rect);
    } else {
        return !RING_IS_EMPTY(&d->ram->cmd_ring);
    }
}

static int _qxl_get_cursor_command(PCIQXLDevice *d, QXLCommand *cmd)
{
    QXLCursorRing *ring;
    int notify;

    if (d->mode == QXL_MODE_VGA) {
        return 0;
    }

    ring = &d->ram->cursor_ring;
    if (RING_IS_EMPTY(ring)) {
        return 0;
    }
    *cmd = *RING_CONS_ITEM(ring);
    RING_POP(ring, notify);
    if (notify) {
        qxl_send_events(d, QXL_INTERRUPT_CURSOR);
    }
    return 1;
}

static const Rect *_qxl_get_update_area(PCIQXLDevice *d)
{
    return &d->ram->update_area;
}

static int _qxl_req_cmd_notification(PCIQXLDevice *d)
{
    int wait;

    if (d->mode == QXL_MODE_VGA) {
        return 1;
    }
    RING_CONS_WAIT(&d->ram->cmd_ring, wait);
    return wait;
}

static int _qxl_req_cursor_notification(PCIQXLDevice *d)
{
    int wait;

    if (d->mode == QXL_MODE_VGA) {
        return 1;
    }
    RING_CONS_WAIT(&d->ram->cursor_ring, wait);
    return wait;
}

#define QXL_FREE_BUNCH_SIZE 10

static inline void qxl_push_free_res(PCIQXLDevice *d)
{
    QXLReleaseRing *ring = &d->ram->release_ring;

    assert(d->mode != QXL_MODE_VGA);
    if (RING_IS_EMPTY(ring) || (d->num_free_res == QXL_FREE_BUNCH_SIZE &&
                                ring->prod - ring->cons + 1 != ring->num_items)) {
        int notify;

        RING_PUSH(ring, notify);
        if (notify) {
            qxl_send_events(d, QXL_INTERRUPT_DISPLAY);
        }
        *RING_PROD_ITEM(ring) = 0;
        d->num_free_res = 0;
        d->last_release = NULL;
    }
}

static void _qxl_release_resource(PCIQXLDevice *d, QXLReleaseInfo *release_info)
{
    UINT64 id = release_info->id;
    QXLReleaseRing *ring;
    UINT64 *item;

    if (d->mode == QXL_MODE_VGA) {
        qemu_free((void *)id);
        return;
    }
    ring = &d->ram->release_ring;
    item = RING_PROD_ITEM(ring);
    if (*item == 0) {
        release_info->next = 0;
        *item = id;
        d->last_release = release_info;
    } else {
        d->last_release->next = release_info->id;
        release_info->next = 0;
        d->last_release = release_info;
    }

    d->num_free_res++;

    qxl_push_free_res(d);
}

static void _qxl_set_save_data(PCIQXLDevice *d, void *data, int size)
{
    qemu_free(d->worker_data);
    d->worker_data = data;
    d->worker_data_size = size;
}

static void *_qxl_get_save_data(PCIQXLDevice *d)
{
    return d->worker_data;
}

static int _qxl_flush_resources(PCIQXLDevice *d)
{
    int ret;
    if (d->mode == QXL_MODE_VGA) {
        return 0;
    }
    ret = d->num_free_res;
    if (ret) {
        qxl_push_free_res(d);
    }
    return ret;
}

static void _qxl_notify_update(PCIQXLDevice *d, uint32_t update_id)
{
    if (d->mode == QXL_MODE_VGA) {
        return;
    }

    d->rom->update_id = update_id;
    qxl_rom_set_dirty(d, &d->rom->update_id, sizeof(d->rom->update_id));
    d->shadow_rom.update_id = update_id;
    qxl_send_events(d, QXL_INTERRUPT_DISPLAY);
}

static void qxl_detach(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_UNDEFINED) {
        return;
    }

    d->worker->detach(d->worker);
    if (d->mode != QXL_MODE_VGA) {
        RING_INIT(&d->ram->cmd_ring);
        RING_INIT(&d->ram->cursor_ring);
        return;
    }
}

static void qxl_set_mode(PCIQXLDevice *d, uint32_t mode)
{
    if (mode > sizeof(qxl_modes) / sizeof(QXLMode)) {
        dprintf(1, "%s: bad mode %u\n", __FUNCTION__, mode);
        return;
    }
    dprintf(1, "%s: %u\n",__FUNCTION__, mode);
    qxl_detach(d);
    assert(RING_IS_EMPTY(&d->ram->cmd_ring));
    assert(RING_IS_EMPTY(&d->ram->cursor_ring));
    qxl_reset_state(d);
    qxl_exit_vga_mode(d);
    d->shadow_rom.mode = mode;
    d->rom->mode = mode;
    qxl_rom_set_dirty(d, &d->rom->mode, sizeof(d->rom->mode));
    memset(d->vram, 0, d->vram_size);
    d->mode = QXL_MODE_NATIVE;
    d->worker->attach(d->worker);
}

static void qxl_add_vga_client(void)
{
    if (qxl_vga.active_clients++ == 0) {
        qemu_mod_timer(qxl_vga.timer, qemu_get_clock(rt_clock));
    }
}

static void qxl_remove_vga_client(void)
{
    qxl_vga.active_clients--;
}

static void qxl_enter_vga_mode(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_VGA || d->id) {
        return;
    }
    dprintf(1, "%u: %s\n", d->id, __FUNCTION__);
    d->rom->mode = ~0;
    qxl_rom_set_dirty(d, &d->rom->mode, sizeof(d->rom->mode));
    d->shadow_rom.mode = ~0;
    d->mode = QXL_MODE_VGA;
    memset(&d->dirty_rect, 0, sizeof(d->dirty_rect));
    qxl_add_vga_client();
}

/* reset the state (assuming the worker is detached) */
static void qxl_reset_state(PCIQXLDevice *d)
{
    QXLRam *ram = d->ram;
    QXLRom *rom = d->rom;

    assert(RING_IS_EMPTY(&ram->cmd_ring));
    assert(RING_IS_EMPTY(&ram->cursor_ring));
    ram->magic = QXL_RAM_MAGIC;
    ram->int_pending = 0;
    ram->int_mask = 0;
    d->shadow_rom.update_id = 0;
    *rom = d->shadow_rom;
    qxl_rom_set_dirty(d, rom, sizeof(*rom));
    RING_INIT(&ram->cmd_ring);
    RING_INIT(&ram->cursor_ring);
    RING_INIT(&ram->release_ring);
    *RING_PROD_ITEM(&ram->release_ring) = 0;
    d->num_free_res = 0;
    d->last_release = NULL;
    memset(&d->dirty_rect, 0, sizeof(d->dirty_rect));
}

/* reset: detach, reset_state, re-attach */
static void qxl_reset(PCIQXLDevice *d)
{
    dprintf(1, "%s\n", __FUNCTION__);
    qxl_detach(d);
    qxl_reset_state(d);
    if (!d->id) {
        qxl_enter_vga_mode(d);
        d->worker->attach(d->worker);
    } else {
        d->mode = QXL_MODE_UNDEFINED;
    }
    qemu_set_irq(d->pci_dev.irq[0], irq_level(d));
}

static void ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    PCIQXLDevice *d = opaque;
    uint32_t io_port = addr - d->io_base;
#ifdef DEBUG_QXL
    dprintf(1, "%s: addr 0x%x val 0x%x\n", __FUNCTION__, addr, val);
#endif
    if (d->mode != QXL_MODE_NATIVE && io_port != QXL_IO_RESET && io_port != QXL_IO_SET_MODE) {
        dprintf(1, "%s: unexpected port 0x%x in vga mode\n", __FUNCTION__, io_port);
        return;
    }
    switch (io_port) {
    case QXL_IO_UPDATE_AREA:
        d->worker->update_area(d->worker);
        break;
    case QXL_IO_NOTIFY_CMD:
        d->worker->wakeup(d->worker);
        break;
    case QXL_IO_NOTIFY_CURSOR:
        d->worker->wakeup(d->worker);
        break;
    case QXL_IO_UPDATE_IRQ:
        qemu_set_irq(d->pci_dev.irq[0], irq_level(d));
        break;
    case QXL_IO_NOTIFY_OOM:
        //todo: add counter
        if (!RING_IS_EMPTY(&d->ram->release_ring)) {
            break;
        }
        pthread_yield();
        if (!RING_IS_EMPTY(&d->ram->release_ring)) {
            break;
        }
        d->worker->oom(d->worker);
        break;
    case QXL_IO_LOG:
        dprintf(1, "%u: %s", d->id, d->ram->log_buf);
        break;
    case QXL_IO_RESET:
        dprintf(1, "%u: QXL_IO_RESET\n", d->id);
        qxl_reset(d);
        break;
    case QXL_IO_SET_MODE:
        dprintf(1, "%u: QXL_IO_SET_MODE\n", d->id);
        qxl_set_mode(d, val);
        break;
    default:
        dprintf(1, "%s: unexpected addr 0x%x val 0x%x\n", __FUNCTION__, addr, val);
    }
}

static uint32_t ioport_read(void *opaque, uint32_t addr)
{
    dprintf(1, "%s: unexpected\n", __FUNCTION__);
    return 0xff;
}

static void qxl_write_config(PCIDevice *d, uint32_t address,
                             uint32_t val, int len)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, d);
    VGACommonState *vga = &qxl->vga;

    if (qxl->id == 0) {
        vga_dirty_log_stop(vga);
    }
    pci_default_write_config(d, address, val, len);
    if (qxl->id == 0) {
        if (vga->map_addr && qxl->pci_dev.io_regions[0].addr == -1)
            vga->map_addr = 0;
        vga_dirty_log_start(vga);
    }
}

static void qxl_ioport_map(PCIDevice *pci_dev, int region_num,
                           pcibus_t addr, pcibus_t size, int type)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, pci_dev);

    dprintf(1, "%s: base 0x%lx size 0x%lx\n", __FUNCTION__, addr, size);
    qxl->io_base = addr;
    register_ioport_write(addr, size, 1, ioport_write, pci_dev);
    register_ioport_read(addr, size, 1, ioport_read, pci_dev);
}

static void qxl_rom_map(PCIDevice *pci_dev, int region_num,
                        pcibus_t addr, pcibus_t size, int type)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, pci_dev);

    dprintf(1, "%s: addr 0x%lx size 0x%lx\n", __FUNCTION__, addr, size);

    assert((addr & (size - 1)) == 0);
    assert(size ==  qxl->rom_size);

    cpu_register_physical_memory(addr, size, qxl->rom_offset | IO_MEM_ROM);
}

static void qxl_ram_map(PCIDevice *pci_dev, int region_num,
                        pcibus_t addr, pcibus_t size, int type)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, pci_dev);

    dprintf(1, "%s: addr 0x%lx size 0x%lx\n", __FUNCTION__, addr, size);

    assert((addr & (size - 1)) == 0);
    assert((addr & ~TARGET_PAGE_MASK) == 0);
    assert(size ==  qxl->vga.vram_size);
    assert((size & ~TARGET_PAGE_MASK) == 0);
    qxl->ram_phys_addr = addr;
    cpu_register_physical_memory(addr, size, qxl->vga.vram_offset | IO_MEM_RAM);

    if (qxl->id == 0) {
        qxl->vga.map_addr = addr;
        qxl->vga.map_end = addr + size;
        vga_dirty_log_start(&qxl->vga);
    }
}

static void qxl_vram_map(PCIDevice *pci_dev, int region_num,
                         pcibus_t addr, pcibus_t size, int type)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, pci_dev);

    dprintf(1, "%s: addr 0x%lx size 0x%lx\n", __FUNCTION__, addr, size);

    assert((addr & (size - 1)) == 0);
    assert((addr & ~TARGET_PAGE_MASK) == 0);
    assert(size ==  qxl->vram_size);
    assert((size & ~TARGET_PAGE_MASK) == 0);
    cpu_register_physical_memory(addr, size, qxl->vram_offset | IO_MEM_RAM);
}

static void init_qxl_rom(PCIQXLDevice *d, uint8_t *buf)
{
    QXLRom *rom = (QXLRom *)buf;
    QXLModes *modes = (QXLModes *)(rom + 1);
    int i;

    rom->magic = QXL_ROM_MAGIC;
    rom->id = d->id;
    rom->mode = 0;
    rom->modes_offset = sizeof(QXLRom);
    rom->draw_area_size = ALIGN(qxl_max_res_area()* sizeof(uint32_t), 4096);
    rom->compression_level = QXL_DEFAULT_COMPRESSION_LEVEL;
    rom->log_level = 0;

    modes->n_modes = sizeof(qxl_modes) / sizeof(QXLMode);

    for (i = 0; i < modes->n_modes; i++) {
        modes->modes[i] = qxl_modes[i];
    }
    d->shadow_rom = *rom;
    d->rom = rom;
    d->modes = modes;
}

static void init_qxl_ram(PCIQXLDevice *d, uint8_t *buf, uint32_t actual_ram_size)
{
    uint32_t draw_area_size;
    uint32_t ram_header_size;

    d->ram_start = buf;

    draw_area_size = d->shadow_rom.draw_area_size;
    ram_header_size = ALIGN(sizeof(*d->ram), 4096);
    assert(ram_header_size + draw_area_size < actual_ram_size);

    d->shadow_rom.ram_header_offset = actual_ram_size - ram_header_size;
    d->ram = (QXLRam *)(buf + d->shadow_rom.ram_header_offset);
    d->ram->magic = QXL_RAM_MAGIC;
    RING_INIT(&d->ram->cmd_ring);
    RING_INIT(&d->ram->cursor_ring);
    RING_INIT(&d->ram->release_ring);
    *RING_PROD_ITEM(&d->ram->release_ring) = 0;

    if (d->id == 0) {
        d->shadow_rom.draw_area_offset = VGA_RAM_SIZE;
    } else {
        d->shadow_rom.draw_area_offset = 0;
    }
    d->shadow_rom.pages_offset = d->shadow_rom.draw_area_offset + draw_area_size;
    d->shadow_rom.num_io_pages = (actual_ram_size - ram_header_size - d->shadow_rom.pages_offset) >> TARGET_PAGE_BITS;

    *d->rom = d->shadow_rom;

    dprintf(1, "qxl device memory layout (device #%d)\n"
            "  vga ram:    0x%08x\n"
            "  draw area:  0x%08x\n"
            "  io pages:   0x%08x (%d pages)\n"
            "  ram header: 0x%08x\n",
            d->id, 0,
            d->shadow_rom.draw_area_offset,
            d->shadow_rom.pages_offset, d->shadow_rom.num_io_pages,
            d->shadow_rom.ram_header_offset);
}

inline uint32_t msb_mask(uint32_t val)
{
    uint32_t mask;

    do {
        mask = ~(val - 1) & val;
        val &= ~mask;
    } while (mask < val);

    return mask;
}

static void qxl_display_update(struct DisplayState *ds, int x, int y, int w, int h)
{
    PCIQXLDevice *client;
    Rect update_area;

    dprintf(2, "%s: x %d y %d w %d h %d\n", __FUNCTION__, x, y, w, h);
    update_area.left = x,
    update_area.right = x + w;
    update_area.top = y;
    update_area.bottom = y + h;

    if (rect_is_empty(&update_area)) {
        return;
    }

    qxl_vga.need_update = false;

    QTAILQ_FOREACH(client, &devs, next) {
        if (client->mode == QXL_MODE_VGA && client->running) {
            int notify = 0;
            pthread_mutex_lock(&dirty_lock);
            if (rect_is_empty(&client->dirty_rect)) {
                notify = 1;
            }
            rect_union(&client->dirty_rect, &update_area);
            pthread_mutex_unlock(&dirty_lock);
            if (notify) {
                client->worker->wakeup(client->worker);
            }
        }
    }
}

static void qxl_vga_update(void)
{
    PCIQXLDevice *client;

    qxl_vga.need_update = false;

    QTAILQ_FOREACH(client, &devs, next) {
        if (client->mode == QXL_MODE_VGA && client->running &&
                !rect_is_empty(&client->dirty_rect)) {
            client->worker->wakeup(client->worker);
        }
    }
}

static void qxl_display_resize(struct DisplayState *ds)
{
    PCIQXLDevice *client;

    QTAILQ_FOREACH(client, &devs, next) {
        if (client->mode == QXL_MODE_VGA) {
            dprintf(1, "%s\n", __FUNCTION__);
            pthread_mutex_lock(&dirty_lock);
            qxl_reset(client);
            pthread_mutex_unlock(&dirty_lock);
        }
    }
}

static void qxl_display_refresh(struct DisplayState *ds)
{
    if (qxl_vga.active_clients) {
        vga_hw_update();
        if (qxl_vga.need_update) {
            qxl_vga_update();
        }
    }
}

#define DISPLAY_REFRESH_INTERVAL 30

static void display_update(void *opaque)
{
    if (!qxl_vga.active_clients) {
        return;
    }
    qxl_display_refresh(qxl_vga.ds);
    qemu_mod_timer(qxl_vga.timer, qemu_get_clock(rt_clock) + DISPLAY_REFRESH_INTERVAL);
}

void qxl_display_init(DisplayState *ds)
{
    DisplayChangeListener* display_listener;
    PCIQXLDevice *dev;

    memset(&qxl_vga, 0, sizeof(qxl_vga));
    qxl_vga.ds = ds;

    display_listener = qemu_mallocz(sizeof(DisplayChangeListener));
    display_listener->dpy_update = qxl_display_update;
    display_listener->dpy_resize = qxl_display_resize;
    display_listener->dpy_refresh = qxl_display_refresh;
    register_displaychangelistener(ds, display_listener);

    qxl_vga.timer = qemu_new_timer(rt_clock, display_update, NULL);
    QTAILQ_FOREACH(dev, &devs, next) {
        if (!dev->id) {
            qxl_enter_vga_mode(dev);
            dev->worker->attach(dev->worker);
        }
    }
}

static void qxl_exit_vga_mode(PCIQXLDevice *d)
{
    if (d->mode != QXL_MODE_VGA) {
        return;
    }
    dprintf(1, "%s\n", __FUNCTION__);
    qxl_remove_vga_client();
    d->mode = QXL_MODE_UNDEFINED;
}

static void qxl_pre_save(void *opaque)
{
    PCIQXLDevice* d = opaque;

    d->worker->save(d->worker);

    if (d->last_release == NULL) {
        d->last_release_offset = 0;
    } else {
        d->last_release_offset = (uint8_t *)d->last_release - d->ram_start;
    }
    assert(d->last_release_offset < d->vga.vram_size);
}

static void free_worker_data(PCIQXLDevice* d)
{
    qemu_free(d->worker_data);
    d->worker_data = NULL;
    d->worker_data_size = 0;
}

static void qxl_post_save(void *opaque)
{
    PCIQXLDevice* d = opaque;

    free_worker_data(d);
}

static int qxl_pre_load(void *opaque)
{
    PCIQXLDevice* d = opaque;

    free_worker_data(d);

    if (d->mode != QXL_MODE_UNDEFINED) {
        d->worker->detach(d->worker);
    }

    if (d->mode == QXL_MODE_VGA) {
        qxl_remove_vga_client();
    }

    return 0;
}

static int qxl_post_load(void *opaque, int version)
{
    PCIQXLDevice* d = opaque;

    if (d->last_release_offset >= d->vga.vram_size) {
        dprintf(1, "%s: invalid last_release_offset %u, ram_size %u\n",
                __FUNCTION__, d->last_release_offset, d->vga.vram_size);
        exit(-1);
    }

    if (d->last_release_offset == 0) {
        d->last_release = NULL;
    } else {
        d->last_release = (QXLReleaseInfo *)(d->ram_start + d->last_release_offset);
    }

    if (d->mode == QXL_MODE_VGA) {
        qxl_add_vga_client();
    }

    if (d->mode != QXL_MODE_UNDEFINED) {
        d->worker->attach(d->worker);
        d->worker->load(d->worker);
    }

    free_worker_data(d);

    return 0;
}

static void qxl_pipe_read(void *opaque)
{
    PCIQXLDevice *d = opaque;
    int len;
    char dummy;

    while (1) {
        len = read(d->pipe_fd[0], &dummy, sizeof(dummy));
        if (len == -1 && errno == EAGAIN)
            break;
        if (len != sizeof(dummy)) {
            dprintf(1, "%s:error reading pipe_fd, len=%d\n", __FUNCTION__, len);
            break;
        }
    }
    qxl_update_irq();
}

static void qxl_vm_change_state_handler(void *opaque, int running, int reason)
{
    PCIQXLDevice *d = opaque;

    dprintf(1, "QXL: %s: running=%d\n", __FUNCTION__, running);

    if (running) {
        qxl_vga.need_update = true;
        d->running = true;
        qemu_set_fd_handler(d->pipe_fd[0], qxl_pipe_read, NULL, d);
        d->worker->start(d->worker);
        qemu_set_irq(d->pci_dev.irq[0], irq_level(d));
        if (qxl_vga.active_clients) {
            qemu_mod_timer(qxl_vga.timer, qemu_get_clock(rt_clock));
        }
    } else {
        qemu_del_timer(qxl_vga.timer);
        d->worker->stop(d->worker);
        qemu_set_fd_handler(d->pipe_fd[0], NULL, NULL, d);
        d->running = false;
    }
}

static void init_pipe_signaling(PCIQXLDevice *d)
{
   if (pipe(d->pipe_fd) < 0) {
       dprintf(1, "%s:pipe creation failed\n", __FUNCTION__);
       return;
   }
#ifdef CONFIG_IOTHREAD
   fcntl(d->pipe_fd[0], F_SETFL, O_NONBLOCK);
#else
   fcntl(d->pipe_fd[0], F_SETFL, O_NONBLOCK | O_ASYNC);
#endif
   fcntl(d->pipe_fd[1], F_SETFL, O_NONBLOCK);
   fcntl(d->pipe_fd[0], F_SETOWN, getpid());
}

static void qxl_reset_handler(DeviceState *dev)
{
    PCIQXLDevice *d = DO_UPCAST(PCIQXLDevice, pci_dev.qdev, dev);
    qxl_reset(d);
}

void qxl_get_info(QXLDevRef dev_ref, QXLDevInfo *info)
{
    _qxl_get_info((PCIQXLDevice *)dev_ref, info);
}

int qxl_get_command(QXLDevRef dev_ref, struct QXLCommand *cmd)
{
    return _qxl_get_command((PCIQXLDevice *)dev_ref, cmd);
}

void qxl_release_resource(QXLDevRef dev_ref, union QXLReleaseInfo *release_info)
{
    _qxl_release_resource((PCIQXLDevice *)dev_ref, release_info);
}

void qxl_notify_update(QXLDevRef dev_ref, uint32_t update_id)
{
    _qxl_notify_update((PCIQXLDevice *)dev_ref, update_id);
}

int qxl_req_cmd_notification(QXLDevRef dev_ref)
{
    return _qxl_req_cmd_notification((PCIQXLDevice *)dev_ref);
}

int qxl_get_cursor_command(QXLDevRef dev_ref, struct QXLCommand *cmd)
{
    return _qxl_get_cursor_command((PCIQXLDevice *)dev_ref, cmd);
}

int qxl_req_cursor_notification(QXLDevRef dev_ref)
{
    return _qxl_req_cursor_notification((PCIQXLDevice *)dev_ref);
}

int qxl_has_command(QXLDevRef dev_ref)
{
    return _qxl_has_command((PCIQXLDevice *)dev_ref);
}

const Rect *qxl_get_update_area(QXLDevRef dev_ref)
{
    return _qxl_get_update_area((PCIQXLDevice *)dev_ref);
}

int qxl_flush_resources(QXLDevRef dev_ref)
{
    return _qxl_flush_resources((PCIQXLDevice *)dev_ref);
}

void qxl_set_save_data(QXLDevRef dev_ref, void *data, int size)
{
    _qxl_set_save_data((PCIQXLDevice *)dev_ref, data, size);
}

void *qxl_get_save_data(QXLDevRef dev_ref)
{
    return _qxl_get_save_data((PCIQXLDevice *)dev_ref);
}

#ifdef CONFIG_SPICE

typedef struct Interface {
    QXLInterface vd_interface;
    PCIQXLDevice *d;
} Interface;

static void interface_attache_worker(QXLInterface *qxl, QXLWorker *qxl_worker)
{
    Interface *interface = DO_UPCAST(Interface, vd_interface, qxl);
    if (interface->d->worker) {
        dprintf(1, "%s: has worker\n", __FUNCTION__);
        exit(-1);
    }
    interface->d->worker = qxl_worker;
}

static void interface_set_compression_level(QXLInterface *qxl, int level)
{
    PCIQXLDevice *d = DO_UPCAST(Interface, vd_interface, qxl)->d;
    d->shadow_rom.compression_level = level;
    d->rom->compression_level = level;
    qxl_rom_set_dirty(d, &d->rom->compression_level,
                      sizeof(d->rom->compression_level));
}

static void interface_set_mm_time(QXLInterface *qxl, uint32_t mm_time)
{
    PCIQXLDevice *d = DO_UPCAST(Interface, vd_interface, qxl)->d;
    d->shadow_rom.mm_clock = mm_time;
    d->rom->mm_clock = mm_time;
    qxl_rom_set_dirty(d, &d->rom->mm_clock, sizeof(d->rom->mm_clock));
}

static VDObjectRef interface_register_mode_change(QXLInterface *qxl,
                                                  qxl_mode_change_notifier_t notifier,
                                                  void *opaque)
{
    /* should not happen, libspice doesn't use this any more */
    fprintf(stderr, "%s: func %p data %p\n", __FUNCTION__, notifier, opaque);
    return 0;
}

static void interface_unregister_mode_change(QXLInterface *qxl, VDObjectRef notifier)
{
}

static void interface_get_info(QXLInterface *qxl, QXLDevInfo *info)
{
    _qxl_get_info(DO_UPCAST(Interface, vd_interface, qxl)->d, info);
}

static int interface_get_command(QXLInterface *qxl, struct QXLCommand *cmd)
{
    return _qxl_get_command(DO_UPCAST(Interface, vd_interface, qxl)->d, cmd);
}

static int interface_req_cmd_notification(QXLInterface *qxl)
{
    return _qxl_req_cmd_notification(DO_UPCAST(Interface, vd_interface, qxl)->d);
}

static int interface_has_command(QXLInterface *qxl)
{
    return _qxl_has_command(DO_UPCAST(Interface, vd_interface, qxl)->d);
}

static void interface_release_resource(QXLInterface *qxl, union QXLReleaseInfo *release_info)
{
    _qxl_release_resource(DO_UPCAST(Interface, vd_interface, qxl)->d, release_info);
}

static int interface_get_cursor_command(QXLInterface *qxl, struct QXLCommand *cmd)
{
    return _qxl_get_cursor_command(DO_UPCAST(Interface, vd_interface, qxl)->d, cmd);
}

static int interface_req_cursor_notification(QXLInterface *qxl)
{
    return _qxl_req_cursor_notification(DO_UPCAST(Interface, vd_interface, qxl)->d);
}

static const struct Rect *interface_get_update_area(QXLInterface *qxl)
{
    return _qxl_get_update_area(DO_UPCAST(Interface, vd_interface, qxl)->d);
}

static void interface_notify_update(QXLInterface *qxl, uint32_t update_id)
{
    _qxl_notify_update(DO_UPCAST(Interface, vd_interface, qxl)->d, update_id);
}

static void interface_set_save_data(QXLInterface *qxl, void *data, int size)
{
    _qxl_set_save_data(((Interface *)qxl)->d, data, size);
}

static void *interface_get_save_data(QXLInterface *qxl)
{
    return _qxl_get_save_data(((Interface *)qxl)->d);
}

static int interface_flush_resources(QXLInterface *qxl)
{
    return _qxl_flush_resources(((Interface *)qxl)->d);
}

static void register_interface(PCIQXLDevice *d)
{
    Interface *interface = qemu_mallocz(sizeof(*interface));

    interface->vd_interface.base.base_version = VM_INTERFACE_VERSION;
    interface->vd_interface.base.type = VD_INTERFACE_QXL;
    interface->vd_interface.base.id = d->id;
    interface->vd_interface.base.description = "QXL GPU";
    interface->vd_interface.base.major_version = VD_INTERFACE_QXL_MAJOR;
    interface->vd_interface.base.minor_version = VD_INTERFACE_QXL_MINOR;

    interface->vd_interface.pci_vendor = REDHAT_PCI_VENDOR_ID;
    interface->vd_interface.pci_id = QXL_DEVICE_ID;
    interface->vd_interface.pci_revision = QXL_REVISION;

    interface->vd_interface.attache_worker = interface_attache_worker;
    interface->vd_interface.set_compression_level = interface_set_compression_level;
    interface->vd_interface.set_mm_time = interface_set_mm_time;
    interface->vd_interface.register_mode_change = interface_register_mode_change;
    interface->vd_interface.unregister_mode_change = interface_unregister_mode_change;

    interface->vd_interface.get_info = interface_get_info;
    interface->vd_interface.get_command = interface_get_command;
    interface->vd_interface.req_cmd_notification = interface_req_cmd_notification;
    interface->vd_interface.has_command = interface_has_command;
    interface->vd_interface.release_resource = interface_release_resource;
    interface->vd_interface.get_cursor_command = interface_get_cursor_command;
    interface->vd_interface.req_cursor_notification = interface_req_cursor_notification;
    interface->vd_interface.get_update_area = interface_get_update_area;
    interface->vd_interface.notify_update = interface_notify_update;
    interface->vd_interface.set_save_data = interface_set_save_data;
    interface->vd_interface.get_save_data = interface_get_save_data;
    interface->vd_interface.flush_resources = interface_flush_resources;

    interface->d = d;
    qemu_spice_add_interface(&interface->vd_interface.base);
}

#endif

static void create_native_worker(PCIQXLDevice *d, int id)
{
    d->worker = qxl_interface_create_worker((QXLDevRef)d, id);
    assert(d->worker);
}

static ram_addr_t qxl_rom_size(void)
{
    uint32_t rom_size = sizeof(QXLRom) + sizeof(QXLModes) + sizeof(qxl_modes);
    rom_size = MAX(rom_size, TARGET_PAGE_SIZE);
    rom_size = msb_mask(rom_size * 2 - 1);
    return rom_size;
}

static int device_id = 0;

static int qxl_init(PCIDevice *dev)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci_dev, dev);
    VGACommonState *vga = &qxl->vga;
    uint8_t* config = qxl->pci_dev.config;
    ram_addr_t ram_size = msb_mask(qxl->vga.vram_size * 2 - 1);
    ram_addr_t rom_size = qxl_rom_size();

    qxl->id = device_id;
    qxl->mode = QXL_MODE_UNDEFINED;
    if (!qxl->id) {
        if (ram_size < 32 * 1024 * 1024)
            ram_size = 32 * 1024 * 1024;
        vga_common_init(vga, ram_size);
        vga_init(vga);
        vga->ds = graphic_console_init(vga->update, vga->invalidate,
                                       vga->screen_dump, vga->text_update, vga);
        qxl_init_modes();
        if (qxl->pci_dev.romfile == NULL)
            qxl->pci_dev.romfile = qemu_strdup("vgabios-qxl.bin");

        pci_config_set_class(config, PCI_CLASS_DISPLAY_VGA);
    } else {
        if (ram_size < 16 * 1024 * 1024)
            ram_size = 16 * 1024 * 1024;
        qxl->vga.vram_size = ram_size;
        qxl->vga.vram_offset = qemu_ram_alloc(qxl->vga.vram_size);

        pci_config_set_class(config, PCI_CLASS_DISPLAY_OTHER);
    }

    pci_config_set_vendor_id(config, REDHAT_PCI_VENDOR_ID);
    pci_config_set_device_id(config, QXL_DEVICE_ID);
    pci_set_byte(&config[PCI_REVISION_ID], QXL_REVISION);
    pci_set_byte(&config[PCI_INTERRUPT_PIN], 1);

    qxl->rom_size = rom_size;
    qxl->rom_offset = qemu_ram_alloc(rom_size);
    init_qxl_rom(qxl, qemu_get_ram_ptr(qxl->rom_offset));
    init_qxl_ram(qxl, qemu_get_ram_ptr(qxl->vga.vram_offset), qxl->vga.vram_size);

    qxl->vram_size = QXL_VRAM_SIZE;
    qxl->vram_offset = qemu_ram_alloc(QXL_VRAM_SIZE);
    qxl->vram = qemu_get_ram_ptr(qxl->vram_offset);

    dprintf(1, "%s: rom(%p, 0x%" PRIx64 ", 0x%x) ram(%p, 0x%" PRIx64 ", 0x%x) vram(%p, 0x%lx, 0x%x)\n",
            __FUNCTION__,
            qxl->rom,
            qxl->rom_offset,
            qxl->rom_size,
            qxl->ram_start,
            qxl->vga.vram_offset,
            qxl->vga.vram_size,
            qxl->vram,
            qxl->vram_offset,
            qxl->vram_size);

    pci_register_bar(&qxl->pci_dev, QXL_IO_RANGE_INDEX,
                     msb_mask(QXL_IO_RANGE_SIZE * 2 - 1),
                     PCI_BASE_ADDRESS_SPACE_IO, qxl_ioport_map);

    pci_register_bar(&qxl->pci_dev, QXL_ROM_RANGE_INDEX,
                     qxl->rom_size, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     qxl_rom_map);

    pci_register_bar(&qxl->pci_dev, QXL_RAM_RANGE_INDEX,
                     qxl->vga.vram_size, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     qxl_ram_map);

    pci_register_bar(&qxl->pci_dev, QXL_VRAM_RANGE_INDEX, qxl->vram_size,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, qxl_vram_map);

    qemu_add_vm_change_state_handler(qxl_vm_change_state_handler, qxl);

    QTAILQ_INSERT_TAIL(&devs, qxl, next);
    main_thread = pthread_self();
    qxl_reset_state(qxl);
    init_pipe_signaling(qxl);

    register_interface(qxl);
    if (!qxl->worker) {
        create_native_worker(qxl, device_id);
    }
    device_id++;

    return 0;
}

void qxl_dev_init(PCIBus *bus)
{
    pci_create_simple(bus, -1, QXL_DEV_NAME);
}

static bool qxl_is_worker_data_exists(void *opaque, int version_id)
{
    PCIQXLDevice* d = opaque;

    if (!d->worker_data_size) {
        return false;
    }

    if (!d->worker_data) {
        d->worker_data = qemu_malloc(d->worker_data_size);
    }

    return true;
}

static VMStateDescription qxl_vmstate = {
    .name = QXL_DEV_NAME,
    .version_id = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .pre_save = qxl_pre_save,
    .post_save = qxl_post_save,
    .pre_load = qxl_pre_load,
    .post_load = qxl_post_load,
    .fields = (VMStateField []) {
        VMSTATE_PCI_DEVICE(pci_dev, PCIQXLDevice),
        VMSTATE_STRUCT(vga, PCIQXLDevice, 0, vmstate_vga_common, VGACommonState),
        VMSTATE_UINT32(shadow_rom.mode, PCIQXLDevice),
        VMSTATE_UINT32(num_free_res, PCIQXLDevice),
        VMSTATE_UINT32(last_release_offset, PCIQXLDevice),
        VMSTATE_UINT32(mode, PCIQXLDevice),
        VMSTATE_UINT32(bits_unique, PCIQXLDevice),
        VMSTATE_PARTIAL_VBUFFER_UINT32(ram_start, PCIQXLDevice, vga.vram_size),
        VMSTATE_INT32(worker_data_size, PCIQXLDevice),
        VMSTATE_VBUFFER(worker_data, PCIQXLDevice, 0, qxl_is_worker_data_exists, 0,
                        worker_data_size),
        VMSTATE_END_OF_LIST()
    }
};

static PCIDeviceInfo qxl_info = {
    .qdev.name = QXL_DEV_NAME,
    .qdev.desc = "Spice QXL GPU",
    .qdev.size = sizeof(PCIQXLDevice),
    .qdev.vmsd = &qxl_vmstate,
    .qdev.reset = qxl_reset_handler,
    .init = qxl_init,
    .config_write = qxl_write_config,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("ram_size", PCIQXLDevice, vga.vram_size, 64 * 1024 * 1024),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void qxl_register(void)
{
    pci_qdev_register(&qxl_info);
}

device_init(qxl_register);

