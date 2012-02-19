/* Define target_phys_addr_t if it exists.  */

#ifndef TARGPHYS_H
#define TARGPHYS_H

typedef uint64_t target_phys_addr_t;
#define TARGET_PHYS_ADDR_MAX UINT64_MAX
#define TARGET_FMT_plx "%016" PRIx64

uint8_t target_ldub(void *data);
uint16_t target_lduw(void *data);
uint32_t target_ldl(void *data);
uint64_t target_ldq(void *data);

void target_stb(void *data, uint8_t value);
void target_stw(void *data, uint16_t value);
void target_stl(void *data, uint32_t value);
void target_stq(void *data, uint64_t value);

#endif
