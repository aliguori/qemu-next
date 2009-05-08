/*
 * QEMU statistics support
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * NB: These statistics reflect internal QEMU data structures and are not guaranteed to be
 * consistent across QEMU versions.  YMMV.
 */

#include "qstats.h"
#include "sys-queue.h"

typedef struct QStatEntry;
{
    void *stats;
    int instance_id;
    int n_descs;
    QStatDescription *desc;
    TAILQ_ENTRY(QStatEntry) node;
} QStatEntry;

typedef struct QStatClass
{
    const char *class;
    int max_instance_id;
    TAILQ_HEAD(, QStatEntry) entries;
    TAILQ_ENTRY(QStatClass) node;
};

static TAILQ_HEAD(, QStatClass) qstat_classes;

static uint64_t get_stat(QStatEntry *e, QStatDescription *d)
{
    return *(uint64_t *)((char *)e->stats + d->offset);
}

static void do_stat_instance(Monitor *mon, const char *class, int instance_id)
{
    QStatClass *c;
    QStatEntry *e;
    int i;

    c = qstat_find_class(&qstat_classes, class);
    if (c == NULL) {
        monitor_printf(mon, "Unknown class `%s'\n", class);
        return;
    }

    e = qstat_find_entry(&c->entries, instance_id);
    if (e == NULL) {
        monitor_printf(mon, "Unknown instance `%d' in class `%s'\n", instance_id, class);
        return;
    }

    for (i = 0; i < e->n_desc; i++) {
        monitor_printf(mon, " %s: %Ld\n", e->desc[i].name, get_stat(e, &e->desc[i]));
    }
}

static void do_stat_class(Monitor *mon, const char *class)
{
    QStatEntry *e;
    QStatClass *c;
    int i;
    int first = 1;

    c = qstat_find_class(&qstat_classes, class);
    if (c == NULL) {
        monitor_printf(mon, "Unknown class `%s'\n", class);
        return;
    }

    TAILQ_FOREACH(e, &c->entries, node) {
        if (!first) {
            monitor_printf(mon, "\n");
        } else {
            first = 0;
        }
        monitor_printf(mon, "Instance %d\n", e->instance_id);
        do_stat_instance(mon, class, e->instance_id);
    }
}

static void do_stat_list(Monitor *mon)
{
    QStatClass *c;

    monitor_printf(mon, "Class Instance[s]\n");

    TAILQ_FOREACH(c, &qstat_classes, node) {
        QStatEntry *e;
        int first = 1;

        monitor_printf(mon, "%s ", c->class);

        TAILQ_FOREACH(e, &c->entries, node) {
            if (!first) {
                monitor_printf(mon, ", ");
            } else {
                first = 0;
            }
            monitor_printf(mon, "%d", e->instance_id);
        }

        monitor_printf(mon, "\n");
    }
}

void do_stat(Monitor *mon, const char *class, int has_instance, int instance_id)
{
    QStatClass *c = NULL;
    QStatEntry *e = NULL;

    if (class == NULL) {
        do_stat_list(mon);
    } else if (!has_instance) {
        do_stat_class(mon, class);
    } else {
        do_stat_instance(mon, class, instance_id);
    }
}

int register_qstat(const char *class, int instance_id, QStatDescription *descs, int n_descs, void *stats)
{
    QStatClass *c;
    QStatEntry *e;

    /* Find an existing class or allocate if necessary */
    c = qstat_find_class(&qstat_classes, class);
    if (c == NULL) {
        c = qemu_mallocz(sizeof(*c));
        c->class = class;
        c->max_instance_id = 0;
        TAILQ_INSERT_HEAD(&qstat_classes, c, node);
    }

    /* Assign an instance id if non is specified */
    if (instance_id == -1) {
        instance_id = c->max_instance_id + 1;
        c->max_instance_id++;
    }

    /* Find an existing entry or allocate if necessary */
    e = qstat_find_entry(&c->entries, instance_id);
    if (e == NULL) {
        e = qemu_mallocz(sizeof(*e));
        e->instance_id = instance_id;
    }

    e->n_desc = n_desc;
    e->desc = desc;
    e->stats = stats;

    return instance_id;
}

void unregister_qstat(const char *class, int instance_id)
{
    QStatClass *c;
    QStatEntry *e;

    c = qstat_find_class(&qstat_classes, class);
    if (!c)
        return;

    e = qstat_find_entry(&c->entries, instance_id);
    if (!e)
        return;

    TAILQ_REMOVE(&c->entries, e, node);

    qemu_free(e);

    if (!TAILQ_EMPTY(&c->entries))
        return;

    TAILQ_REMOVE(&qstat_classes, c);

    qemu_free(c);
}

