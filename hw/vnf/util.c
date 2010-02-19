#include "vnf/util.h"

size_t iovec_length(struct iovec *iov, int iovcnt)
{
    size_t size = 0;
    int i;

    for (i = 0; i < iovcnt; i++) {
        size += iov[i].iov_len;
    }

    return size;
}
