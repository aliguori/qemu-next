#ifndef QEMU_INTERFACE_H
#define QEMU_INTERFACE_H

#define MAX_IFACE_NAME 64

typedef struct Interface Interface;

struct Interface
{
    char name[MAX_IFACE_NAME];
};

static inline Interface *iface_find(const char *name,
                                    const char *kind)
{
    return NULL;
}

#endif
