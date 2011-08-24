/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/module.h"

#include <glib.h>

typedef struct ModuleEntry
{
    module_init_type type;
    void (*init)(void);
} ModuleEntry;

static GSList *init_type_list[MODULE_INIT_MAX];

static GSList **find_type(module_init_type type)
{
    return &init_type_list[type];
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;
    GSList **l;

    e = g_malloc0(sizeof(*e));
    e->init = fn;

    l = find_type(type);

    *l = g_slist_prepend(*l, e);
}

void module_call_init(module_init_type type)
{
    GSList *l, *i;

    l = *find_type(type);

    for (i = l; i; i = i->next) {
        ModuleEntry *e = i->data;
        e->init();
    }
}
