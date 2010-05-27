/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#include "qdict.h"
#include "qemu-common.h"
#include "notify.h"
#include "monitor.h"

#define MIG_STATE_ERROR		-1
#define MIG_STATE_COMPLETED	0
#define MIG_STATE_CANCELLED	1
#define MIG_STATE_ACTIVE	2

typedef struct MigrationState MigrationState;

struct MigrationState
{
    /* FIXME: add more accessors to print migration info */
    void (*cancel)(MigrationState *s);
    int (*get_status)(MigrationState *s);
    void (*release)(MigrationState *s);
    int blk;
    int shared;
    QError *error;
};

typedef struct FdMigrationState FdMigrationState;

struct FdMigrationState
{
    MigrationState mig_state;
    int64_t bandwidth_limit;
    QEMUFile *file;
    int fd;
    Notifier *notifier;
    int state;
    int (*close)(struct FdMigrationState*);
    int (*write)(struct FdMigrationState*, const void *, size_t);
    int (*get_error)(struct FdMigrationState*);
    void *opaque;
};

void qemu_start_incoming_migration(const char *uri);

int do_migrate(Monitor *mon, const QDict *qdict, MonitorCompletion *cb,
               void *opaque);

int do_migrate_cancel(Monitor *mon, const QDict *qdict, QObject **ret_data);

int do_migrate_set_speed(Monitor *mon, const QDict *qdict, QObject **ret_data);

uint64_t migrate_max_downtime(void);

int do_migrate_set_downtime(Monitor *mon, const QDict *qdict,
                            QObject **ret_data);

void do_info_migrate_print(Monitor *mon, const QObject *data);

void do_info_migrate(Monitor *mon, QObject **ret_data);

int exec_start_incoming_migration(const char *host_port);

MigrationState *exec_start_outgoing_migration(const char *host_port,
					      int64_t bandwidth_limit,
					      int blk,
					      int inc,
                                              Notifier *notifier);

int tcp_start_incoming_migration(const char *host_port);

MigrationState *tcp_start_outgoing_migration(const char *host_port,
					     int64_t bandwidth_limit,
					     int blk,
					     int inc,
                                             Notifier *notifier);

int unix_start_incoming_migration(const char *path);

MigrationState *unix_start_outgoing_migration(const char *path,
					      int64_t bandwidth_limit,
					      int blk,
					      int inc,
                                              Notifier *notifier);

int fd_start_incoming_migration(const char *path);

MigrationState *fd_start_outgoing_migration(int fd,
					    int64_t bandwidth_limit,
					    int blk,
					    int inc,
                                            Notifier *notifier);

void migrate_fd_error(FdMigrationState *s, QError *err);

void migrate_fd_cleanup(FdMigrationState *s);

void migrate_fd_put_notify(void *opaque);

ssize_t migrate_fd_put_buffer(void *opaque, const void *data, size_t size);

void migrate_fd_connect(FdMigrationState *s);

void migrate_fd_put_ready(void *opaque);

int migrate_fd_get_status(MigrationState *mig_state);

void migrate_fd_cancel(MigrationState *mig_state);

void migrate_fd_release(MigrationState *mig_state);

void migrate_fd_wait_for_unfreeze(void *opaque);

int migrate_fd_close(void *opaque);

static inline FdMigrationState *migrate_to_fms(MigrationState *mig_state)
{
    return container_of(mig_state, FdMigrationState, mig_state);
}

#endif
