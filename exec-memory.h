#ifndef EXEC_MEMORY_H
#define EXEC_MEMORY_H

/*
 * Internal interfaces between memory.c/exec.c/vl.c.  Do not #include unless
 * you're one of them.
 */

#include "memory.h"

#ifndef CONFIG_USER_ONLY

/* Get the root memory region.  This interface should only be used temporarily
 * until a proper bus interface is available.
 */
MemoryRegion *get_system_memory(void);

MemoryRegion *get_system_io(void);

/* Set the root memory region.  This region is the system memory map. */
void set_system_memory_map(MemoryRegion *mr);

/* Set the I/O memory region.  This region is the I/O memory map. */
void set_system_io_map(MemoryRegion *mr);

#endif

#endif
