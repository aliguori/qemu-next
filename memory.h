#ifndef MEMORY_H
#define MEMORY_H

#ifndef CONFIG_USER_ONLY

#include <stdint.h>
#include <stdbool.h>
#include "qemu-common.h"
#include "cpu-common.h"
#include "targphys.h"
#include "qemu-queue.h"
#include "iorange.h"
#include "ioport.h"

typedef struct MemoryRegionOps MemoryRegionOps;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionPortio MemoryRegionPortio;
typedef struct MemoryRegionMmio MemoryRegionMmio;

/* Must match *_DIRTY_FLAGS in cpu-all.h.  To be replaced with dynamic
 * registration.
 */
#define DIRTY_MEMORY_VGA       0
#define DIRTY_MEMORY_CODE      1
#define DIRTY_MEMORY_MIGRATION 3

struct MemoryRegionMmio {
    CPUReadMemoryFunc *read[3];
    CPUWriteMemoryFunc *write[3];
};

/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     target_phys_addr_t addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  target_phys_addr_t addr,
                  uint64_t data,
                  unsigned size);

    enum device_endian endianness;
    /* Guest-visible constraints: */
    struct {
        /* If nonzero, specify bounds on access sizes beyond which a machine
         * check is thrown.
         */
        unsigned min_access_size;
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise unaligned
         * accesses throw machine checks.
         */
         bool unaligned;
    } valid;
    /* Internal implementation constraints: */
    struct {
        /* If nonzero, specifies the minimum size implemented.  Smaller sizes
         * will be rounded upwards and a partial result will be returned.
         */
        unsigned min_access_size;
        /* If nonzero, specifies the maximum size implemented.  Larger sizes
         * will be done as a series of accesses with smaller sizes.
         */
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise all accesses
         * are converted to (possibly multiple) naturally aligned accesses.
         */
         bool unaligned;
    } impl;

    /* If .read and .write are not present, old_portio may be used for
     * backwards compatibility with old portio registration
     */
    const MemoryRegionPortio *old_portio;
    /* If .read and .write are not present, old_mmio may be used for
     * backwards compatibility with old mmio registration
     */
    const MemoryRegionMmio old_mmio;
};

typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

struct MemoryRegion {
    /* All fields are private - violators will be prosecuted */
    const MemoryRegionOps *ops;
    void *opaque;
    MemoryRegion *parent;
    uint64_t size;
    target_phys_addr_t addr;
    target_phys_addr_t offset;
    bool backend_registered;
    ram_addr_t ram_addr;
    IORange iorange;
    bool terminates;
    MemoryRegion *alias;
    target_phys_addr_t alias_offset;
    unsigned priority;
    bool may_overlap;
    QTAILQ_HEAD(subregions, MemoryRegion) subregions;
    QTAILQ_ENTRY(MemoryRegion) subregions_link;
    QTAILQ_HEAD(coalesced_ranges, CoalescedMemoryRange) coalesced;
    const char *name;
    uint8_t dirty_log_mask;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

struct MemoryRegionPortio {
    uint32_t offset;
    uint32_t len;
    unsigned size;
    IOPortReadFunc *read;
    IOPortWriteFunc *write;
};

#define PORTIO_END { }

/* Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.
 */
void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size);
/* Initialize an I/O memory region.  Accesses into the region will be
 * cause the callbacks in @ops to be called.
 *
 * if @size is nonzero, subregions will be clipped to @size.
 */
void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);
/* Initialize an I/O memory region.  Accesses into the region will be
 * modify memory directly.
 */
void memory_region_init_ram(MemoryRegion *mr,
                            DeviceState *dev, /* FIXME: layering violation */
                            const char *name,
                            uint64_t size);
/* Initialize a RAM memory region.  Accesses into the region will be
 * modify memory in @ptr directly.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                DeviceState *dev, /* FIXME: layering violation */
                                const char *name,
                                uint64_t size,
                                void *ptr);
/* Initializes a memory region which aliases a section of another memory
 * region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size);

/* Destroy a memory region.  The memory becomes inaccessible. */
void memory_region_destroy(MemoryRegion *mr);

target_phys_addr_t memory_region_size(MemoryRegion *mr);

/* Get a pointer into a RAM memory region; use with care */
void *memory_region_get_ram_ptr(MemoryRegion *mr);

/* Sets an offset to be added to MemoryRegionOps callbacks.  This function
 * is deprecated and should not be used in new code. */
void memory_region_set_offset(MemoryRegion *mr, target_phys_addr_t offset);

/* Turn logging on or off for specified client (display, migration) */
void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client);

/* Check whether a page is dirty for a specified client. */
bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client);

/* Mark a page as dirty in a memory region, after it has been dirtied outside
 * guest code
 */
void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr);

/* Synchronize a region's dirty bitmap with any external TLBs (e.g. kvm) */
void memory_region_sync_dirty_bitmap(MemoryRegion *mr);

/* Mark a range of pages as not dirty, for a specified client. */
void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client);

/* Turn a memory region read-only (or read-write) */
void memory_region_set_readonly(MemoryRegion *mr, bool readonly);

/* Enable memory coalescing for the region.  MMIO ->write callbacks may be
 * delayed until a non-coalesced MMIO is issued.
 */
void memory_region_set_coalescing(MemoryRegion *mr);

/* Enable memory coalescing for a sub-range of the region.  MMIO ->write
 * callbacks may be delayed until a non-coalesced MMIO is issued.
 */
void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size);
/* Disable MMIO coalescing for the region. */
void memory_region_clear_coalescing(MemoryRegion *mr);


/* Request an eventfd to be triggered when a word is written to a location */
void memory_region_add_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);

/* Cancel an existing eventfd  */
void memory_region_del_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);

/* Add a sub-region at @offset.  The sub-region may not overlap with other
 * subregions (except for those explicitly marked as overlapping)
 */
void memory_region_add_subregion(MemoryRegion *mr,
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion);
/* Add a sub-region at @offset.  The sub-region may overlap other subregions;
 * conflicts are resolved by having a higher @priority hide a lower @priority.
 * Subregions without priority are taken as @priority 0.
 */
void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority);
/* Remove a subregion. */
void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion);

#endif

#endif
