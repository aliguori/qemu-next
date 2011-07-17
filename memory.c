/*
 * Physical memory management
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "memory.h"
#include "exec-memory.h"
#include "ioport.h"
#include "bitops.h"
#include "kvm.h"
#include <assert.h>

typedef struct AddrRange AddrRange;

struct AddrRange {
    uint64_t start;
    uint64_t size;
};

static AddrRange addrrange_make(uint64_t start, uint64_t size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_equal(AddrRange r1, AddrRange r2)
{
    return r1.start == r2.start && r1.size == r2.size;
}

static uint64_t addrrange_end(AddrRange r)
{
    return r.start + r.size;
}

static AddrRange addrrange_shift(AddrRange range, int64_t delta)
{
    range.start += delta;
    return range;
}

static bool addrrange_intersects(AddrRange r1, AddrRange r2)
{
    return (r1.start >= r2.start && r1.start < r2.start + r2.size)
        || (r2.start >= r1.start && r2.start < r1.start + r1.size);
}

static AddrRange addrrange_intersection(AddrRange r1, AddrRange r2)
{
    uint64_t start = MAX(r1.start, r2.start);
    /* off-by-one arithmetic to prevent overflow */
    uint64_t end = MIN(addrrange_end(r1) - 1, addrrange_end(r2) - 1);
    return addrrange_make(start, end - start + 1);
}

struct CoalescedMemoryRange {
    AddrRange addr;
    QTAILQ_ENTRY(CoalescedMemoryRange) link;
};

struct MemoryRegionIoeventfd {
    AddrRange addr;
    bool match_data;
    uint64_t data;
    int fd;
};

static bool memory_region_ioeventfd_before(MemoryRegionIoeventfd a,
                                           MemoryRegionIoeventfd b)
{
    if (a.addr.start < b.addr.start) return true;
    if (a.addr.start > b.addr.start) return false;
    if (a.addr.size < b.addr.size) return true;
    if (a.addr.size > b.addr.size) return false;
    if (a.match_data < b.match_data) return true;
    if (a.match_data > b.match_data) return false;
    if (a.match_data) {
        if (a.data < b.data) return true;
        if (a.data > b.data) return false;
    }
    if (a.fd < b.fd) return true;
    if (a.fd > b.fd) return false;
    return false;
}

static bool memory_region_ioeventfd_equal(MemoryRegionIoeventfd a,
                                          MemoryRegionIoeventfd b)
{
    return !memory_region_ioeventfd_before(a, b)
        && !memory_region_ioeventfd_before(b, a);
}

typedef struct FlatRange FlatRange;
typedef struct FlatView FlatView;

/* Range of memory in the global map.  Addresses are absolute. */
struct FlatRange {
    MemoryRegion *mr;
    target_phys_addr_t offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
};

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 */
struct FlatView {
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
};

typedef struct AddressSpace AddressSpace;
typedef struct AddressSpaceOps AddressSpaceOps;

/* A system address space - I/O, memory, etc. */
struct AddressSpace {
    const AddressSpaceOps *ops;
    MemoryRegion *root;
    FlatView current_map;
    int ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

struct AddressSpaceOps {
    void (*range_add)(AddressSpace *as, FlatRange *fr);
    void (*range_del)(AddressSpace *as, FlatRange *fr);
    void (*log_start)(AddressSpace *as, FlatRange *fr);
    void (*log_stop)(AddressSpace *as, FlatRange *fr);
    void (*ioeventfd_add)(AddressSpace *as, MemoryRegionIoeventfd *fd);
    void (*ioeventfd_del)(AddressSpace *as, MemoryRegionIoeventfd *fd);
};

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region;
}

static void flatview_init(FlatView *view)
{
    view->ranges = NULL;
    view->nr = 0;
    view->nr_allocated = 0;
}

/* Insert a range into a given position.  Caller is responsible for maintaining
 * sorting order.
 */
static void flatview_insert(FlatView *view, unsigned pos, FlatRange *range)
{
    if (view->nr == view->nr_allocated) {
        view->nr_allocated = MAX(2 * view->nr, 10);
        view->ranges = qemu_realloc(view->ranges,
                                    view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos,
            (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    ++view->nr;
}

static void flatview_destroy(FlatView *view)
{
    qemu_free(view->ranges);
}

/* Attempt to simplify a view by merging ajacent ranges */
static void flatview_simplify(FlatView *view)
{
    unsigned i;
    FlatRange *r1, *r2;

    for (i = 0; i + 1 < view->nr; ++i) {
        r1 = &view->ranges[i];
        r2 = &view->ranges[i+1];
        if (addrrange_end(r1->addr) == r2->addr.start
            && r1->mr == r2->mr
            && r1->offset_in_region + r1->addr.size == r2->offset_in_region
            && r1->dirty_log_mask == r2->dirty_log_mask) {
            r1->addr.size += r2->addr.size;
            memmove(r2, r2 + 1, (view->nr - (i + 2)) * sizeof(*r2));
            --view->nr;
            --i;
        }
    }
}

static void flatview_dump(FlatView *view)
{
    unsigned i;
    FlatRange *r;

    printf("memory map:\n");
    for (i = 0; i < view->nr; ++i) {
        r = &view->ranges[i];
        printf("flatrange[%d]: %"PRIx64"-%"PRIx64" (%s@%"PRIx64")\n",
               i, r->addr.start, r->addr.start + r->addr.size - 1, r->mr->name,
               (uint64_t)r->offset_in_region);
    }
}

static void memory_region_prepare_ram_addr(MemoryRegion *mr);

static void as_memory_range_add(AddressSpace *as, FlatRange *fr)
{
    ram_addr_t phys_offset, region_offset;

    memory_region_prepare_ram_addr(fr->mr);

    phys_offset = fr->mr->ram_addr;
    region_offset = fr->offset_in_region;
    /* cpu_register_physical_memory_log() wants region_offset for
     * mmio, but prefers offseting phys_offset for RAM.  Humour it.
     */
    if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM) {
        phys_offset += region_offset;
        region_offset = 0;
    }

    cpu_register_physical_memory_log(fr->addr.start,
                                     fr->addr.size,
                                     phys_offset,
                                     region_offset,
                                     fr->dirty_log_mask);
}

static void as_memory_range_del(AddressSpace *as, FlatRange *fr)
{
    cpu_register_physical_memory(fr->addr.start, fr->addr.size,
                                 IO_MEM_UNASSIGNED);
}

static void as_memory_log_start(AddressSpace *as, FlatRange *fr)
{
    cpu_physical_log_start(fr->addr.start, fr->addr.size);
}

static void as_memory_log_stop(AddressSpace *as, FlatRange *fr)
{
    cpu_physical_log_stop(fr->addr.start, fr->addr.size);
}

static void as_memory_ioeventfd_add(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    if (!fd->match_data || fd->addr.size != 4) {
        abort();
    }

    r = kvm_set_ioeventfd_mmio_long(fd->fd, fd->addr.start, fd->data, true);
    if (r < 0) {
        abort();
    }
}

static void as_memory_ioeventfd_del(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    r = kvm_set_ioeventfd_mmio_long(fd->fd, fd->addr.start, fd->data, false);
    if (r < 0) {
        abort();
    }
}

static const AddressSpaceOps address_space_ops_memory = {
    .range_add = as_memory_range_add,
    .range_del = as_memory_range_del,
    .log_start = as_memory_log_start,
    .log_stop = as_memory_log_stop,
    .ioeventfd_add = as_memory_ioeventfd_add,
    .ioeventfd_del = as_memory_ioeventfd_del,
};

static AddressSpace address_space_memory = {
    .ops = &address_space_ops_memory,
};

static const MemoryRegionPortio *find_portio(MemoryRegion *mr, uint64_t offset,
                                             unsigned width, bool write)
{
    const MemoryRegionPortio *mrp;

    for (mrp = mr->ops->old_portio; mrp->size; ++mrp) {
        if (offset >= mrp->offset && offset < mrp->offset + mrp->len
            && width == mrp->size
            && (write ? (bool)mrp->write : (bool)mrp->read)) {
            return mrp;
        }
    }
    return NULL;
}

static void memory_region_iorange_read(IORange *iorange,
                                       uint64_t offset,
                                       unsigned width,
                                       uint64_t *data)
{
    MemoryRegion *mr = container_of(iorange, MemoryRegion, iorange);

    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset, width, false);

        *data = ((uint64_t)1 << (width * 8)) - 1;
        if (mrp) {
            *data = mrp->read(mr->opaque, offset - mrp->offset);
        }
        return;
    }
    *data = mr->ops->read(mr->opaque, offset, width);
}

static void memory_region_iorange_write(IORange *iorange,
                                        uint64_t offset,
                                        unsigned width,
                                        uint64_t data)
{
    MemoryRegion *mr = container_of(iorange, MemoryRegion, iorange);

    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset, width, true);

        if (mrp) {
            mrp->write(mr->opaque, offset - mrp->offset, data);
        }
        return;
    }
    mr->ops->write(mr->opaque, offset, data, width);
}

static const IORangeOps memory_region_iorange_ops = {
    .read = memory_region_iorange_read,
    .write = memory_region_iorange_write,
};

static void as_io_range_add(AddressSpace *as, FlatRange *fr)
{
    iorange_init(&fr->mr->iorange, &memory_region_iorange_ops,
                 fr->addr.start,fr->addr.size);
    ioport_register(&fr->mr->iorange);
}

static void as_io_range_del(AddressSpace *as, FlatRange *fr)
{
    isa_unassign_ioport(fr->addr.start, fr->addr.size);
}

static void as_io_ioeventfd_add(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    if (!fd->match_data || fd->addr.size != 2) {
        abort();
    }

    r = kvm_set_ioeventfd_pio_word(fd->fd, fd->addr.start, fd->data, true);
    if (r < 0) {
        abort();
    }
}

static void as_io_ioeventfd_del(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    r = kvm_set_ioeventfd_pio_word(fd->fd, fd->addr.start, fd->data, false);
    if (r < 0) {
        abort();
    }
}

static const AddressSpaceOps address_space_ops_io = {
    .range_add = as_io_range_add,
    .range_del = as_io_range_del,
    .ioeventfd_add = as_io_ioeventfd_add,
    .ioeventfd_del = as_io_ioeventfd_del,
};

static AddressSpace address_space_io = {
    .ops = &address_space_ops_io,
};

/* Render a memory region into the global view.  Ranges in @view obscure
 * ranges in @mr.
 */
static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 target_phys_addr_t base,
                                 AddrRange clip)
{
    MemoryRegion *subregion;
    unsigned i;
    target_phys_addr_t offset_in_region;
    uint64_t remain;
    uint64_t now;
    FlatRange fr;
    AddrRange tmp;

    base += mr->addr;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        base -= mr->alias->addr;
        base -= mr->alias_offset;
        render_memory_region(view, mr->alias, base, clip);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = clip.start - base;
    base = clip.start;
    remain = clip.size;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && remain; ++i) {
        if (base >= addrrange_end(view->ranges[i].addr)) {
            continue;
        }
        if (base < view->ranges[i].addr.start) {
            now = MIN(remain, view->ranges[i].addr.start - base);
            fr.mr = mr;
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            fr.dirty_log_mask = mr->dirty_log_mask;
            flatview_insert(view, i, &fr);
            ++i;
            base += now;
            offset_in_region += now;
            remain -= now;
        }
        if (base == view->ranges[i].addr.start) {
            now = MIN(remain, view->ranges[i].addr.size);
            base += now;
            offset_in_region += now;
            remain -= now;
        }
    }
    if (remain) {
        fr.mr = mr;
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        fr.dirty_log_mask = mr->dirty_log_mask;
        flatview_insert(view, i, &fr);
    }
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView generate_memory_topology(MemoryRegion *mr)
{
    FlatView view;

    flatview_init(&view);

    render_memory_region(&view, mr, 0, addrrange_make(0, UINT64_MAX));
    flatview_simplify(&view);

    flatview_dump(&view);

    return view;
}

static void address_space_add_del_ioeventfds(AddressSpace *as,
                                             MemoryRegionIoeventfd *fds_new,
                                             unsigned fds_new_nb,
                                             MemoryRegionIoeventfd *fds_old,
                                             unsigned fds_old_nb)
{
    unsigned iold, inew;

    /* Generate a symmetric difference of the old and new fd sets, adding
     * and deleting as necessary.
     */

    iold = inew = 0;
    while (iold < fds_old_nb || inew < fds_new_nb) {
        if (iold < fds_old_nb
            && (inew == fds_new_nb
                || memory_region_ioeventfd_before(fds_old[iold],
                                                  fds_new[inew]))) {
            as->ops->ioeventfd_del(as, &fds_old[iold]);
            ++iold;
        } else if (inew < fds_new_nb
                   && (iold == fds_old_nb
                       || memory_region_ioeventfd_before(fds_new[inew],
                                                         fds_old[iold]))) {
            as->ops->ioeventfd_add(as, &fds_new[inew]);
            ++inew;
        } else {
            ++iold;
            ++inew;
        }
    }
}

static void address_space_update_ioeventfds(AddressSpace *as)
{
    FlatRange *fr;
    unsigned ioeventfd_nb = 0;
    MemoryRegionIoeventfd *ioeventfds = NULL;
    AddrRange tmp;
    unsigned i;

    FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
        for (i = 0; i < fr->mr->ioeventfd_nb; ++i) {
            tmp = addrrange_shift(fr->mr->ioeventfds[i].addr,
                                  fr->addr.start - fr->offset_in_region);
            if (addrrange_intersects(fr->addr, tmp)) {
                ++ioeventfd_nb;
                ioeventfds = qemu_realloc(ioeventfds,
                                          ioeventfd_nb * sizeof(*ioeventfds));
                ioeventfds[ioeventfd_nb-1] = fr->mr->ioeventfds[i];
                ioeventfds[ioeventfd_nb-1].addr = tmp;
            }
        }
    }

    address_space_add_del_ioeventfds(as, ioeventfds, ioeventfd_nb,
                                     as->ioeventfds, as->ioeventfd_nb);

    qemu_free(as->ioeventfds);
    as->ioeventfds = ioeventfds;
    as->ioeventfd_nb = ioeventfd_nb;
}

static void address_space_update_topology(AddressSpace *as)
{
    FlatView old_view = as->current_map;
    FlatView new_view = generate_memory_topology(as->root);
    unsigned iold, inew;
    FlatRange *frold, *frnew;

    /* Generate a symmetric difference of the old and new memory maps.
     * Kill ranges in the old map, and instantiate ranges in the new map.
     */
    iold = inew = 0;
    while (iold < old_view.nr || inew < new_view.nr) {
        if (iold < old_view.nr) {
            frold = &old_view.ranges[iold];
        } else {
            frold = NULL;
        }
        if (inew < new_view.nr) {
            frnew = &new_view.ranges[inew];
        } else {
            frnew = NULL;
        }

        if (frold
            && (!frnew
                || frold->addr.start < frnew->addr.start
                || (frold->addr.start == frnew->addr.start
                    && !flatrange_equal(frold, frnew)))) {
            /* In old, but (not in new, or in new but attributes changed). */

            printf("dropping %"PRIx64"-%"PRIx64"\n", frold->addr.start,
                   frold->addr.start + frold->addr.size - 1);

            as->ops->range_del(as, frold);
            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both (logging may have changed) */

            if (frold->dirty_log_mask && !frnew->dirty_log_mask) {
                as->ops->log_stop(as, frnew);
            } else if (frnew->dirty_log_mask && !frold->dirty_log_mask) {
                as->ops->log_start(as, frnew);
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            printf("adding %"PRIx64"-%"PRIx64" offset %"PRIx64" ram %lx\n",
                   frnew->addr.start,
                   frnew->addr.start + frnew->addr.size - 1,
                   (uint64_t)frnew->offset_in_region,
                   frnew->mr->ram_addr);

            as->ops->range_add(as, frnew);
            ++inew;
        }
    }
    as->current_map = new_view;
    flatview_destroy(&old_view);
    address_space_update_ioeventfds(as);
}

static void memory_region_update_topology(void)
{
    if (address_space_memory.root) {
        address_space_update_topology(&address_space_memory);
    }
    if (address_space_io.root) {
        address_space_update_topology(&address_space_io);
    }
}

void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size)
{
    mr->ops = NULL;
    mr->parent = NULL;
    mr->size = size;
    mr->addr = 0;
    mr->offset = 0;
    mr->terminates = false;
    mr->priority = 0;
    mr->may_overlap = false;
    mr->alias = NULL;
    QTAILQ_INIT(&mr->subregions);
    memset(&mr->subregions_link, 0, sizeof mr->subregions_link);
    QTAILQ_INIT(&mr->coalesced);
    mr->name = qemu_strdup(name);
    mr->dirty_log_mask = 0;
    mr->ioeventfd_nb = 0;
    mr->ioeventfds = NULL;
}

static bool memory_region_access_valid(MemoryRegion *mr,
                                       target_phys_addr_t addr,
                                       unsigned size)
{
    if (!mr->ops->valid.unaligned && (addr & (size - 1))) {
        return false;
    }

    /* Treat zero as compatibility all valid */
    if (!mr->ops->valid.max_access_size) {
        return true;
    }

    if (size > mr->ops->valid.max_access_size
        || size < mr->ops->valid.min_access_size) {
        return false;
    }
    return true;
}

static uint32_t memory_region_read_thunk_n(void *_mr,
                                           target_phys_addr_t addr,
                                           unsigned size)
{
    MemoryRegion *mr = _mr;
    unsigned access_size, access_size_min, access_size_max;
    uint64_t access_mask;
    uint32_t data = 0, tmp;
    unsigned i;

    if (!memory_region_access_valid(mr, addr, size)) {
        return -1U; /* FIXME: better signalling */
    }

    if (!mr->ops->read) {
        return mr->ops->old_mmio.read[bitops_ffsl(size)](mr->opaque, addr);
    }

    /* FIXME: support unaligned access */

    access_size_min = mr->ops->impl.min_access_size;
    if (!access_size_min) {
        access_size_min = 1;
    }
    access_size_max = mr->ops->impl.max_access_size;
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    addr += mr->offset;
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        tmp = mr->ops->read(mr->opaque, addr + i, access_size);
        data |= (tmp & access_mask) << (i * 8);
    }

    return data;
}

static void memory_region_write_thunk_n(void *_mr,
                                        target_phys_addr_t addr,
                                        unsigned size,
                                        uint64_t data)
{
    MemoryRegion *mr = _mr;
    unsigned access_size, access_size_min, access_size_max;
    uint64_t access_mask;
    unsigned i;

    printf("write: region %s addr %"PRIx64" size %d data %"PRIx64"\n",
           mr->name, (uint64_t)addr, size, data);

    if (!memory_region_access_valid(mr, addr, size)) {
        return; /* FIXME: better signalling */
    }

    if (!mr->ops->write) {
        mr->ops->old_mmio.write[bitops_ffsl(size)](mr->opaque, addr, data);
        return;
    }

    /* FIXME: support unaligned access */

    access_size_min = mr->ops->impl.min_access_size;
    if (!access_size_min) {
        access_size_min = 1;
    }
    access_size_max = mr->ops->impl.max_access_size;
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    addr += mr->offset;
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        printf(" -> write: addr %"PRIx64" data %"PRIx64" size %d\n",
               (uint64_t)addr + i, (data >> (i * 8)) & access_mask,
               access_size);
        mr->ops->write(mr->opaque, addr + i, (data >> (i * 8)) & access_mask,
                       access_size);
    }
}

static uint32_t memory_region_read_thunk_b(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 1);
}

static uint32_t memory_region_read_thunk_w(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 2);
}

static uint32_t memory_region_read_thunk_l(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 4);
}

static void memory_region_write_thunk_b(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 1, data);
}

static void memory_region_write_thunk_w(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 2, data);
}

static void memory_region_write_thunk_l(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 4, data);
}

static CPUReadMemoryFunc * const memory_region_read_thunk[] = {
    memory_region_read_thunk_b,
    memory_region_read_thunk_w,
    memory_region_read_thunk_l,
};

static CPUWriteMemoryFunc * const memory_region_write_thunk[] = {
    memory_region_write_thunk_b,
    memory_region_write_thunk_w,
    memory_region_write_thunk_l,
};

static void memory_region_prepare_ram_addr(MemoryRegion *mr)
{
    if (mr->backend_registered) {
        return;
    }

    mr->ram_addr = cpu_register_io_memory(memory_region_read_thunk,
                                          memory_region_write_thunk,
                                          mr,
                                          mr->ops->endianness);
    mr->backend_registered = true;
}

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->backend_registered = false;
}

void memory_region_init_ram(MemoryRegion *mr,
                            DeviceState *dev,
                            const char *name,
                            uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->terminates = true;
    mr->ram_addr = qemu_ram_alloc(dev, name, size);
    mr->backend_registered = true;
}

void memory_region_init_ram_ptr(MemoryRegion *mr,
                                DeviceState *dev,
                                const char *name,
                                uint64_t size,
                                void *ptr)
{
    memory_region_init(mr, name, size);
    mr->terminates = true;
    mr->ram_addr = qemu_ram_alloc_from_ptr(dev, name, size, ptr);
    mr->backend_registered = true;
}

void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

void memory_region_destroy(MemoryRegion *mr)
{
    assert(QTAILQ_EMPTY(&mr->subregions));
    memory_region_clear_coalescing(mr);
    qemu_free((char *)mr->name);
    qemu_free(mr->ioeventfds);
}

target_phys_addr_t memory_region_size(MemoryRegion *mr)
{
    return mr->size;
}

void memory_region_set_offset(MemoryRegion *mr, target_phys_addr_t offset)
{
    mr->offset = offset;
}

void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client)
{
    uint8_t mask = 1 << client;

    mr->dirty_log_mask = (mr->dirty_log_mask & ~mask) | (log * mask);
    memory_region_update_topology();
}

bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client)
{
    assert(mr->terminates);
    return cpu_physical_memory_get_dirty(mr->ram_addr + addr, 1 << client);
}

void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr)
{
    assert(mr->terminates);
    return cpu_physical_memory_set_dirty(mr->ram_addr + addr);
}

void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    FlatRange *fr;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            cpu_physical_sync_dirty_bitmap(fr->addr.start,
                                           fr->addr.start + fr->addr.size);
        }
    }
}

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    /* FIXME */
}

void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client)
{
    assert(mr->terminates);
    cpu_physical_memory_reset_dirty(mr->ram_addr + addr,
                                    mr->ram_addr + addr + size,
                                    1 << client);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_ram_ptr(mr->alias) + mr->alias_offset;
    }

    if (!mr->terminates) {
        abort();
    }

    return qemu_get_ram_ptr(mr->ram_addr);
}

static void memory_region_update_coalesced_range(MemoryRegion *mr)
{
    FlatRange *fr;
    CoalescedMemoryRange *cmr;
    AddrRange tmp;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            qemu_unregister_coalesced_mmio(fr->addr.start, fr->addr.size);
            QTAILQ_FOREACH(cmr, &mr->coalesced, link) {
                tmp = addrrange_shift(cmr->addr,
                                      fr->addr.start - fr->offset_in_region);
                if (!addrrange_intersects(tmp, fr->addr)) {
                    continue;
                }
                tmp = addrrange_intersection(tmp, fr->addr);
                qemu_register_coalesced_mmio(tmp.start, tmp.size);
            }
        }
    }
}

void memory_region_set_coalescing(MemoryRegion *mr)
{
    memory_region_clear_coalescing(mr);
    memory_region_add_coalescing(mr, 0, mr->size);
}

void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size)
{
    CoalescedMemoryRange *cmr = qemu_malloc(sizeof(*cmr));

    cmr->addr = addrrange_make(offset, size);
    QTAILQ_INSERT_TAIL(&mr->coalesced, cmr, link);
    memory_region_update_coalesced_range(mr);
}

void memory_region_clear_coalescing(MemoryRegion *mr)
{
    CoalescedMemoryRange *cmr;

    while (!QTAILQ_EMPTY(&mr->coalesced)) {
        cmr = QTAILQ_FIRST(&mr->coalesced);
        QTAILQ_REMOVE(&mr->coalesced, cmr, link);
        qemu_free(cmr);
    }
    memory_region_update_coalesced_range(mr);
}

void memory_region_add_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = addr,
        .addr.size = size,
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_before(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    ++mr->ioeventfd_nb;
    mr->ioeventfds = qemu_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds) * mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i+1], &mr->ioeventfds[i],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb-1 - i));
    mr->ioeventfds[i] = mrfd;
    memory_region_update_topology();
}

void memory_region_del_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = addr,
        .addr.size = size,
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_equal(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    if (i == mr->ioeventfd_nb) {
        abort();
    }
    memmove(&mr->ioeventfds[i], &mr->ioeventfds[i+1],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb - (i+1)));
    --mr->ioeventfd_nb;
    mr->ioeventfds = qemu_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds)*mr->ioeventfd_nb + 1);
    memory_region_update_topology();
}

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               target_phys_addr_t offset,
                                               MemoryRegion *subregion)
{
    MemoryRegion *other;

    assert(!subregion->parent);
    subregion->parent = mr;
    subregion->addr = offset;
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->may_overlap || other->may_overlap) {
            continue;
        }
        if (offset >= other->offset + other->size
            || offset + subregion->size <= other->offset) {
            continue;
        }
        printf("warning: subregion collision %llx/%llx vs %llx/%llx\n",
               (unsigned long long)offset,
               (unsigned long long)subregion->size,
               (unsigned long long)other->offset,
               (unsigned long long)other->size);
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_topology();
}


void memory_region_add_subregion(MemoryRegion *mr,
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion)
{
    subregion->may_overlap = false;
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority)
{
    subregion->may_overlap = true;
    subregion->priority = priority;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion)
{
    assert(subregion->parent == mr);
    subregion->parent = NULL;
    QTAILQ_REMOVE(&mr->subregions, subregion, subregions_link);
    memory_region_update_topology();
}

void set_system_memory_map(MemoryRegion *mr)
{
    address_space_memory.root = mr;
    memory_region_update_topology();
}

void set_system_io_map(MemoryRegion *mr)
{
    address_space_io.root = mr;
    memory_region_update_topology();
}
