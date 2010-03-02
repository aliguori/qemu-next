/*
 *  virtual page mapping and translated block handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

#include "cpu.h"
#include "kvm.h"

#define MAX_QEMU_RAM_SLOTS 20

typedef struct RAMBlock {
    uint8_t *host;
    ram_addr_t offset;
    ram_addr_t length;
    struct RAMBlock *next;
} RAMBlock;

typedef struct QemuRamSlot
{
    target_phys_addr_t start_addr;
    ram_addr_t size;
    ram_addr_t offset;
    int ref;
} QemuRamSlot;

static RAMBlock *ram_blocks;
/* TODO: When we implement (and use) ram deallocation (e.g. for hotplug)
   then we can no longer assume contiguous ram offsets, and external uses
   of this variable will break.  */
ram_addr_t last_ram_offset;

static int num_ram_slots;
static QemuRamSlot ram_slots[MAX_QEMU_RAM_SLOTS];
static int ram_skip_check;

static int in_slot(target_phys_addr_t needle,
                   target_phys_addr_t haystack, ram_addr_t size)
{
    if (needle >= haystack && needle < (haystack + size)) {
        return 1;
    }
    return 0;
}

static QemuRamSlot *qemu_ram_find_slot(target_phys_addr_t start_addr,
                                       ram_addr_t size)
{
    int i;

    for (i = 0; i < num_ram_slots; i++) {
        QemuRamSlot *s = &ram_slots[i];

        if (s->start_addr == start_addr && s->size == size) {
            return s;
        }

        if (in_slot(start_addr, s->start_addr, s->size) ||
            in_slot(start_addr + size - 1, s->start_addr, s->size) ||
            in_slot(s->start_addr, start_addr, size) ||
            in_slot(s->start_addr + s->size - 1, start_addr, size)) {
            abort();
        }
    }

    return NULL;
}

void qemu_ram_check_overlap(target_phys_addr_t start, ram_addr_t size)
{
    QemuRamSlot *s;

    if (ram_skip_check) {
        return;
    }

    s = qemu_ram_find_slot(start, size);
    assert(s == NULL);
}

void qemu_ram_register(target_phys_addr_t start_addr, ram_addr_t size)
{
    QemuRamSlot *s;

    s = qemu_ram_find_slot(start_addr, size);
    assert(s == NULL);
    assert(num_ram_slots < MAX_QEMU_RAM_SLOTS);

    s = &ram_slots[num_ram_slots++];

    s->start_addr = start_addr;
    s->size = size;
    s->offset = qemu_ram_alloc(size);
    s->ref = 0;

    ram_skip_check = 1;
    cpu_register_physical_memory(s->start_addr, s->size, s->offset);
    ram_skip_check = 0;
}

int qemu_ram_unregister(target_phys_addr_t start_addr, ram_addr_t size)
{
    QemuRamSlot *s;
    int i;

    s = qemu_ram_find_slot(start_addr, size);
    assert(s != NULL);

    if (s->ref > 0) {
        return -EAGAIN;
    }

    i = ((s - ram_slots) / sizeof(ram_slots[0]));
    memcpy(&ram_slots[i], &ram_slots[i + 1],
           (num_ram_slots - i) * sizeof(ram_slots[0]));
    num_ram_slots--;

    ram_skip_check = 1;
    cpu_register_physical_memory(start_addr, size, IO_MEM_UNASSIGNED);
    ram_skip_check = 0;

    return 0;
}

void *qemu_ram_map(target_phys_addr_t start_addr, ram_addr_t *size)
{
    QemuRamSlot *s;
    int i;

    for (i = 0; i < num_ram_slots; i++) {
        s = &ram_slots[i];

        if (in_slot(start_addr, s->start_addr, s->size)) {
            assert(in_slot(start_addr + *size - 1,
                           s->start_addr, s->size));
            break;
        }
    }
    assert(i < num_ram_slots);

    s->ref++;

    return qemu_get_ram_ptr(s->offset + (start_addr - s->start_addr));
}

void qemu_ram_unmap(void *addr)
{
    ram_addr_t offset = qemu_ram_addr_from_host(addr);
    QemuRamSlot *s;
    int i;

    for (i = 0; i < num_ram_slots; i++) {
        s = &ram_slots[i];

        if (offset >= s->offset && offset < (s->offset + s->size)) {
            break;
        }
    }
    assert(i < num_ram_slots);

    s->ref--;
}

void qemu_ram_alias(target_phys_addr_t src_addr, target_phys_addr_t dst_addr,
                    ram_addr_t size)
{
    QemuRamSlot *s, *d;

    s = qemu_ram_find_slot(src_addr, size);
    assert(s != NULL);
    assert(num_ram_slots < MAX_QEMU_RAM_SLOTS);

    d = &ram_slots[num_ram_slots++];
    d->start_addr = dst_addr;
    d->size = size;
    d->offset = s->offset;
    d->ref = 0;
}

ram_addr_t qemu_ram_alloc(ram_addr_t size)
{
    RAMBlock *new_block;

    size = TARGET_PAGE_ALIGN(size);
    new_block = qemu_malloc(sizeof(*new_block));

#if defined(TARGET_S390X) && defined(CONFIG_KVM)
    /* XXX S390 KVM requires the topmost vma of the RAM to be < 256GB */
    new_block->host = mmap((void*)0x1000000, size, PROT_EXEC|PROT_READ|PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
#else
    new_block->host = qemu_vmalloc(size);
#endif
#ifdef MADV_MERGEABLE
    madvise(new_block->host, size, MADV_MERGEABLE);
#endif
    new_block->offset = last_ram_offset;
    new_block->length = size;

    new_block->next = ram_blocks;
    ram_blocks = new_block;

    phys_ram_dirty = qemu_realloc(phys_ram_dirty,
        (last_ram_offset + size) >> TARGET_PAGE_BITS);
    memset(phys_ram_dirty + (last_ram_offset >> TARGET_PAGE_BITS),
           0xff, size >> TARGET_PAGE_BITS);

    last_ram_offset += size;

    if (kvm_enabled())
        kvm_setup_guest_memory(new_block->host, size);

    return new_block->offset;
}

void qemu_ram_free(ram_addr_t addr)
{
    /* TODO: implement this.  */
}

/* Return a host pointer to ram allocated with qemu_ram_alloc.
   With the exception of the softmmu code in this file, this should
   only be used for local memory (e.g. video ram) that the device owns,
   and knows it isn't going to access beyond the end of the block.

   It should not be used for general purpose DMA.
   Use cpu_physical_memory_map/cpu_physical_memory_rw instead.
 */
void *qemu_get_ram_ptr(ram_addr_t addr)
{
    RAMBlock *prev;
    RAMBlock **prevp;
    RAMBlock *block;

    prev = NULL;
    prevp = &ram_blocks;
    block = ram_blocks;
    while (block && (block->offset > addr
                     || block->offset + block->length <= addr)) {
        if (prev)
          prevp = &prev->next;
        prev = block;
        block = block->next;
    }
    if (!block) {
        fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
        abort();
    }
    /* Move this entry to to start of the list.  */
    if (prev) {
        prev->next = block->next;
        block->next = *prevp;
        *prevp = block;
    }
    return block->host + (addr - block->offset);
}

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr)
{
    RAMBlock *prev;
    RAMBlock *block;
    uint8_t *host = ptr;

    prev = NULL;
    block = ram_blocks;
    while (block && (block->host > host
                     || block->host + block->length <= host)) {
        prev = block;
        block = block->next;
    }
    if (!block) {
        fprintf(stderr, "Bad ram pointer %p\n", ptr);
        abort();
    }
    return block->offset + (host - block->host);
}
