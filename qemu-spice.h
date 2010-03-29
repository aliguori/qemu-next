#ifndef QEMU_SPICE_H
#define QEMU_SPICE_H

#ifdef CONFIG_SPICE

#include <spice.h>

#include "qemu-option.h"
#include "qemu-config.h"
#include "qdict.h"

struct VDInterface;
extern int using_spice;

void qemu_spice_init(void);
void qemu_spice_input_init(SpiceServer *s);
void qemu_spice_tablet_size(int width, int height);
void qemu_spice_display_init(DisplayState *ds);

void qxl_display_init(DisplayState *ds);
void qxl_dev_init(PCIBus *bus);

void qemu_spice_add_interface(struct VDInterface *interface);
void qemu_spice_remove_interface(struct VDInterface *interface);

void qemu_spice_migrate_start(void);
void qemu_spice_migrate_end(int completed);

int mon_set_password(Monitor *mon, const QDict *qdict, QObject **ret_data);
void mon_spice_migrate(Monitor *mon, const QDict *qdict, QObject **ret_data);

#else  /* CONFIG_SPICE */

#define using_spice 0

#endif /* CONFIG_SPICE */

#endif /* QEMU_SPICE_H */
