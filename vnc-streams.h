#ifndef QEMU_VNC_STREAMS_H
#define QEMU_VNC_STREAMS_H

#include "vnc.h"
#include "qemu-char.h"

CharDriverState *qemu_chr_open_vnc(const char *name);

void vnc_streams_enumerate(VncState *vs);
void vnc_streams_detach(VncState *vs);
void vnc_streams_read(VncState *vs, int id, uint32_t len, void *data);


#endif
