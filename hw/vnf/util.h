#ifndef QEMU_VNF_UTIL_H
#define QEMU_VNF_UTIL_H

#include <sys/uio.h>

#define PAGE_SIZE 4096

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define offset_of(type, member) \
    ((unsigned long)&((type *)0)->member)
#define container_of(type, member, obj) \
    ((type *)((char *)obj - offset_of(type, member)))
#define wmb() do { } while (0)

size_t iovec_length(struct iovec *iov, int iovcnt);

#endif
