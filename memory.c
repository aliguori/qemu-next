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
#include "address-space.h"
#include "exec-memory.h"
#include "bitops.h"
#include "cpu-physmem.h"
#include <assert.h>

unsigned memory_region_transaction_depth = 0;

static AddrRange addrrange_make(int64_t start, int64_t size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_equal(AddrRange r1, AddrRange r2)
{
    return r1.start == r2.start && r1.size == r2.size;
}

static int64_t addrrange_end(AddrRange r)
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
    int64_t start = MAX(r1.start, r2.start);
    /* off-by-one arithmetic to prevent overflow */
    int64_t end = MIN(addrrange_end(r1) - 1, addrrange_end(r2) - 1);
    return addrrange_make(start, end - start + 1);
}

static bool memory_region_ioeventfd_before(MemoryRegionIoeventfd a,
                                           MemoryRegionIoeventfd b)
{
    if (a.addr.start < b.addr.start) {
        return true;
    } else if (a.addr.start > b.addr.start) {
        return false;
    } else if (a.addr.size < b.addr.size) {
        return true;
    } else if (a.addr.size > b.addr.size) {
        return false;
    } else if (a.match_data < b.match_data) {
        return true;
    } else  if (a.match_data > b.match_data) {
        return false;
    } else if (a.match_data) {
        if (a.data < b.data) {
            return true;
        } else if (a.data > b.data) {
            return false;
        }
    }
    if (a.fd < b.fd) {
        return true;
    } else if (a.fd > b.fd) {
        return false;
    }
    return false;
}

static bool memory_region_ioeventfd_equal(MemoryRegionIoeventfd a,
                                          MemoryRegionIoeventfd b)
{
    return !memory_region_ioeventfd_before(a, b)
        && !memory_region_ioeventfd_before(b, a);
}

static QTAILQ_HEAD(, AddressSpace) address_spaces =
    QTAILQ_HEAD_INITIALIZER(address_spaces);

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region
        && a->readable == b->readable;
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

static bool can_merge(FlatRange *r1, FlatRange *r2)
{
    return addrrange_end(r1->addr) == r2->addr.start
        && r1->mr == r2->mr
        && r1->offset_in_region + r1->addr.size == r2->offset_in_region
        && r1->dirty_log_mask == r2->dirty_log_mask
        && r1->readable == r2->readable;
}

/* Attempt to simplify a view by merging ajacent ranges */
static void flatview_simplify(FlatView *view)
{
    unsigned i, j;

    i = 0;
    while (i < view->nr) {
        j = i + 1;
        while (j < view->nr
               && can_merge(&view->ranges[j-1], &view->ranges[j])) {
            view->ranges[i].addr.size += view->ranges[j].addr.size;
            ++j;
        }
        ++i;
        memmove(&view->ranges[i], &view->ranges[j],
                (view->nr - j) * sizeof(view->ranges[j]));
        view->nr -= j - i;
    }
}

void memory_region_read_accessor(void *opaque,
                                 target_phys_addr_t addr,
                                 uint64_t *value,
                                 unsigned size,
                                 unsigned shift,
                                 uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = mr->ops->read(mr->opaque, addr, size);
    *value |= (tmp & mask) << shift;
}

void memory_region_write_accessor(void *opaque,
                                  target_phys_addr_t addr,
                                  uint64_t *value,
                                  unsigned size,
                                  unsigned shift,
                                  uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = (*value >> shift) & mask;
    mr->ops->write(mr->opaque, addr, tmp, size);
}

void access_with_adjusted_size(target_phys_addr_t addr,
                               uint64_t *value,
                               unsigned size,
                               unsigned access_size_min,
                               unsigned access_size_max,
                               void (*access)(void *opaque,
                                              target_phys_addr_t addr,
                                              uint64_t *value,
                                              unsigned size,
                                              unsigned shift,
                                              uint64_t mask),
                               void *opaque)
{
    uint64_t access_mask;
    unsigned access_size;
    unsigned i;

    if (!access_size_min) {
        access_size_min = 1;
    }
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        access(opaque, addr + i, value, access_size, i * 8, access_mask);
    }
}

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
    int64_t remain;
    int64_t now;
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
            fr.readable = mr->readable;
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
        fr.readable = mr->readable;
        flatview_insert(view, i, &fr);
    }
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView generate_memory_topology(MemoryRegion *mr)
{
    FlatView view;

    flatview_init(&view);

    render_memory_region(&view, mr, 0, addrrange_make(0, INT64_MAX));
    flatview_simplify(&view);

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

static void address_space_update_topology_pass(AddressSpace *as,
                                               FlatView old_view,
                                               FlatView new_view,
                                               bool adding)
{
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

            if (!adding) {
                as->ops->range_del(as, frold);
            }

            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both (logging may have changed) */

            if (adding) {
                if (frold->dirty_log_mask && !frnew->dirty_log_mask) {
                    as->ops->log_stop(as, frnew);
                } else if (frnew->dirty_log_mask && !frold->dirty_log_mask) {
                    as->ops->log_start(as, frnew);
                }
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            if (adding) {
                as->ops->range_add(as, frnew);
            }

            ++inew;
        }
    }
}

static void address_space_update_topology(AddressSpace *as)
{
    FlatView old_view = as->current_map;
    FlatView new_view = generate_memory_topology(as->root);

    address_space_update_topology_pass(as, old_view, new_view, false);
    address_space_update_topology_pass(as, old_view, new_view, true);

    as->current_map = new_view;
    flatview_destroy(&old_view);
    address_space_update_ioeventfds(as);
}

void address_space_add(AddressSpace *as)
{
    QTAILQ_INSERT_TAIL(&address_spaces, as, link);
}

void memory_region_update_topology(void)
{
    AddressSpace *as;

    if (memory_region_transaction_depth) {
        return;
    }

    QTAILQ_FOREACH(as, &address_spaces, link) {
        if (as->root) {
            address_space_update_topology(as);
        }
    }
}

void memory_region_transaction_begin(void)
{
    ++memory_region_transaction_depth;
}

void memory_region_transaction_commit(void)
{
    assert(memory_region_transaction_depth);
    --memory_region_transaction_depth;
    memory_region_update_topology();
}

static void memory_region_destructor_none(MemoryRegion *mr)
{
}

static void memory_region_destructor_ram(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_addr);
}

static void memory_region_destructor_ram_from_ptr(MemoryRegion *mr)
{
    qemu_ram_free_from_ptr(mr->ram_addr);
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
    mr->readable = true;
    mr->destructor = memory_region_destructor_none;
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

bool memory_region_access_valid(MemoryRegion *mr,
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
    mr->destructor = memory_region_destructor_ram;
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
    mr->destructor = memory_region_destructor_ram_from_ptr;
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
    mr->destructor(mr);
    memory_region_clear_coalescing(mr);
    qemu_free((char *)mr->name);
    qemu_free(mr->ioeventfds);
}

uint64_t memory_region_size(MemoryRegion *mr)
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

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    /* FIXME */
}

void memory_region_rom_device_set_readable(MemoryRegion *mr, bool readable)
{
    if (mr->readable != readable) {
        mr->readable = readable;
        memory_region_update_topology();
    }
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_ram_ptr(mr->alias) + mr->alias_offset;
    }

    assert(mr->terminates);

    return qemu_get_ram_ptr(mr->ram_addr);
}

static AddressSpace *memory_region_find_as(MemoryRegion *mr)
{
    AddressSpace *as;

    QTAILQ_FOREACH(as, &address_spaces, link) {
        FlatRange *fr;

        FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
            if (fr->mr == mr) {
                return as;
            }
        }
    }

    return NULL;
}

bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client)
{
    AddressSpace *as = memory_region_find_as(mr);

    return as->ops->get_dirty(as, mr, addr, client);
}

void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr)
{
    AddressSpace *as = memory_region_find_as(mr);

    as->ops->set_dirty(as, mr, addr);
}

void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    AddressSpace *as = memory_region_find_as(mr);

    as->ops->sync_dirty_bitmap(as, mr);
}

void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client)
{
    AddressSpace *as = memory_region_find_as(mr);

    as->ops->reset_dirty(as, mr, addr, size, client);
}


static void memory_region_update_coalesced_range(MemoryRegion *mr)
{
    AddressSpace *as;

    QTAILQ_FOREACH(as, &address_spaces, link) {
        FlatRange *fr;

        FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
            if (fr->mr == mr) {
                as->ops->update_coalesced_range(as, fr);
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
    assert(i != mr->ioeventfd_nb);
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
