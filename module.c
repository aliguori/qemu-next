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

#include "qemu-common.h"
#include "sys-queue.h"
#include "module.h"

typedef struct ModuleEntry
{
    int priority;
    int is_init;
    union {
        int (*init)(void);
        void (*exit)(void);
    };
    TAILQ_ENTRY(ModuleEntry) node;
} ModuleEntry;

typedef struct ModulePriorityList
{
    int priority;
    TAILQ_HEAD(, ModuleEntry) entry_list;
    TAILQ_ENTRY(ModulePriorityList) node;
} ModulePriorityList;

static TAILQ_HEAD(, ModulePriorityList) priority_list;

static ModulePriorityList *find_priority_or_alloc(int priority, int alloc)
{
    ModulePriorityList *n;

    TAILQ_FOREACH(n, &priority_list, node) {
        if (priority >= n->priority)
            break;
    }

    if (!n || n->priority != priority) {
        ModulePriorityList *o;

        if (!alloc)
            return NULL;

        o = qemu_mallocz(sizeof(*o));
        o->priority = priority;
        TAILQ_INIT(&o->entry_list);

        if (n) {
            TAILQ_INSERT_AFTER(&priority_list, n, o, node);
        } else {
            TAILQ_INSERT_HEAD(&priority_list, o, node);
        }

        n = o;
    }

    return n;
}

void register_module_init(int (*fn)(void), int priority)
{
    ModuleEntry *e;
    ModulePriorityList *l;

    e = qemu_mallocz(sizeof(*e));
    e->is_init = 1;
    e->init = fn;

    l = find_priority_or_alloc(priority, 1);

    TAILQ_INSERT_TAIL(&l->entry_list, e, node);
}

void register_module_exit(void (*fn)(void), int priority)
{
    ModuleEntry *e;
    ModulePriorityList *l;

    e = qemu_mallocz(sizeof(*e));
    e->is_init = 0;
    e->exit = fn;

    l = find_priority_or_alloc(priority, 1);

    TAILQ_INSERT_TAIL(&l->entry_list, e, node);
}

int module_call_init(int priority)
{
    ModulePriorityList *l;
    ModuleEntry *e;

    l = find_priority_or_alloc(priority, 0);
    if (!l) {
        return 0;
    }

    TAILQ_FOREACH(e, &l->entry_list, node) {
        int ret;

        if (!e->is_init) {
            continue;
        }

        ret = e->init();
        if (ret != 0)
            return ret;
    }

    return 0;
}

void module_call_exit(int priority)
{
    ModulePriorityList *l;
    ModuleEntry *e;

    l = find_priority_or_alloc(priority, 0);
    if (!l) {
        return;
    }

    TAILQ_FOREACH(e, &l->entry_list, node) {
        if (!e->is_init) {
            e->exit();
        }
    }
}
