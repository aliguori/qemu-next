#ifndef QEMU_MEMORY_H
#define QEMU_MEMORY_H

/* Register a raw area of ram */
ram_addr_t qemu_ram_alloc(ram_addr_t addr);

/* Free registered ram area */
void qemu_ram_free(ram_addr_t addr);

/* This should only be used for ram local to a device.  */
void *qemu_get_ram_ptr(ram_addr_t addr);

/* This should not be used by devices.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr);

/* Register a region of ram memory
 *
 * The region must not overlap with another region.  It cannot be split
 * by any other region.  Any type an address is mapped in this type of
 * memory, a call to unregister will fail until the mapping is unmapped.
 */
void qemu_ram_register(target_phys_addr_t start_addr, ram_addr_t size);

/* Unregister a region of ram memory
 *
 * This function will fail if any mappings are active.
 */
int qemu_ram_unregister(target_phys_addr_t start_addr, ram_addr_t size);

/* Alias a region of ram memory
 *
 * An aliased region points to another region.  After aliasing, each region
 * still behaves as an independent region.  IOW, you can unregister each
 * region indepedently of the other.
 */
void qemu_ram_alias(target_phys_addr_t src_addr, target_phys_addr_t dst_addr,
                    ram_addr_t size);

/* Validate that a given region of physical memory does not overlap
 * with an existing ram mapping.
 */
void qemu_ram_check_overlap(target_phys_addr_t start, ram_addr_t size);

void *qemu_ram_map(target_phys_addr_t start_addr, ram_addr_t size);

void qemu_ram_set_dirty(void *addr, ram_addr_t size);

void qemu_ram_unmap(void *addr);

#endif
