#ifndef QEMU_SPICE_H
#define QEMU_SPICE_H

#ifdef CONFIG_SPICE

#include <spice.h>

#include "qemu-option.h"
#include "qemu-config.h"

struct VDInterface;
extern int using_spice;

void qemu_spice_init(void);
void qemu_spice_input_init(SpiceServer *s);

void qemu_spice_add_interface(struct VDInterface *interface);
void qemu_spice_remove_interface(struct VDInterface *interface);

#else  /* CONFIG_SPICE */

#define using_spice 0

#endif /* CONFIG_SPICE */

#endif /* QEMU_SPICE_H */
