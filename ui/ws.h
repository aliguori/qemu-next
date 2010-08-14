#ifndef QEMU_WS_H
#define QEMU_WS_H

#include "qemu-common.h"

int ws_compute_challenge(const char *key1,
                         const char *key2,
                         const uint8_t *challenge,
                         uint8_t *response);

#endif
