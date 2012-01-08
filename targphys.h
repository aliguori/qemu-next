/* Define target_phys_addr_t if it exists.  */

#ifndef TARGPHYS_H
#define TARGPHYS_H

typedef uint64_t target_phys_addr_t;
#define TARGET_PHYS_ADDR_MAX UINT64_MAX
#define TARGET_FMT_plx "%016" PRIx64

#endif
