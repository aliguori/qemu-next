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

#include "qemu-common.h"
#include "migration.h"
#include "monitor.h"
#include "buffered_file.h"
#include "sysemu.h"
#include "block.h"
#include "qemu_socket.h"
#include "block-migration.h"
#include "qemu-objects.h"

//#define DEBUG_MIGRATION

#ifdef DEBUG_MIGRATION
#define DPRINTF(fmt, ...) \
    do { printf("migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* Migration speed throttling */
static int64_t max_throttle = (32 << 20);

static MigrationState *current_migration;

int qemu_start_incoming_migration(const char *uri)
{
    const char *p;
    int ret;

    if (strstart(uri, "tcp:", &p))
        ret = tcp_start_incoming_migration(p);
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        ret =  exec_start_incoming_migration(p);
    else if (strstart(uri, "unix:", &p))
        ret = unix_start_incoming_migration(p);
    else if (strstart(uri, "fd:", &p))
        ret = fd_start_incoming_migration(p);
#endif
    else {
        fprintf(stderr, "unknown migration protocol: %s\n", uri);
        ret = -EPROTONOSUPPORT;
    }
    return ret;
}

void process_incoming_migration(QEMUFile *f)
{
    if (qemu_loadvm_state(f) < 0) {
        fprintf(stderr, "load of migration failed\n");
        exit(0);
    }
    qemu_announce_self();
    DPRINTF("successfully loaded vm state\n");

    incoming_expected = false;

    if (autostart)
        vm_start();
}

void qmp_migrate(const char *uri, bool has_blk, bool blk,
                 bool has_inc, bool inc, Error **errp)
{
    MigrationState *s = NULL;
    const char *p;

    if (current_migration &&
        current_migration->get_status(current_migration) == MIG_STATE_ACTIVE) {
        error_set(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    if (!qemu_savevm_can_migrate(errp)) {
        return;
    }

    if (strstart(uri, "tcp:", &p)) {
        s = tcp_start_outgoing_migration(NULL, p, max_throttle, true,
                                         blk, inc);
#if !defined(WIN32)
    } else if (strstart(uri, "exec:", &p)) {
        s = exec_start_outgoing_migration(NULL, p, max_throttle, true,
                                          blk, inc);
    } else if (strstart(uri, "unix:", &p)) {
        s = unix_start_outgoing_migration(NULL, p, max_throttle, true,
                                          blk, inc);
    } else if (strstart(uri, "fd:", &p)) {
        s = fd_start_outgoing_migration(NULL, p, max_throttle, true, 
                                        blk, inc);
#endif
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "uri", "a valid migration protocol");
        return;
    }

    if (s == NULL) {
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }

    if (current_migration) {
        current_migration->release(current_migration);
    }

    current_migration = s;
}

void qmp_migrate_cancel(Error **errp)
{
    MigrationState *s = current_migration;

    if (s) {
        s->cancel(s);
    }
}

void qmp_migrate_set_speed(int64_t value, Error **errp)
{
    FdMigrationState *s;

    if (value < 0) {
        value = 0;
    }
    max_throttle = value;

    s = migrate_to_fms(current_migration);
    if (s && s->file) {
        qemu_file_set_rate_limit(s->file, max_throttle);
    }
}

/* amount of nanoseconds we are willing to wait for migration to be down.
 * the choice of nanoseconds is because it is the maximum resolution that
 * get_clock() can achieve. It is an internal measure. All user-visible
 * units must be in seconds */
static uint64_t max_downtime = 30000000;

uint64_t migrate_max_downtime(void)
{
    return max_downtime;
}

void qmp_migrate_set_downtime(double value, Error **errp)
{
    value *= 1e9;
    value = MAX(0, MIN(UINT64_MAX, value));
    max_downtime = (uint64_t)value;
}

MigrationInfo *qmp_query_migrate(Error **errp)
{
    MigrationState *s = current_migration;
    MigrationInfo *info = qmp_alloc_migration_info();

    if (s) {
        switch (s->get_status(s)) {
        case MIG_STATE_ACTIVE:
            info->has_status = true;
            info->status = qemu_strdup("active");

            info->has_ram = true;
            info->ram = qmp_alloc_migration_stats();
            info->ram->transferred = ram_bytes_transferred();
            info->ram->remaining = ram_bytes_remaining();
            info->ram->total = ram_bytes_total();

            if (blk_mig_active()) {
                info->has_disk = true;
                info->disk = qmp_alloc_migration_stats();
                info->disk->transferred = blk_mig_bytes_transferred();
                info->disk->remaining = blk_mig_bytes_remaining();
                info->disk->total = blk_mig_bytes_total();
            }
            break;
        case MIG_STATE_COMPLETED:
            info->has_status = true;
            info->status = qemu_strdup("completed");
            break;
        case MIG_STATE_ERROR:
            info->has_status = true;
            info->status = qemu_strdup("failed");
            break;
        case MIG_STATE_CANCELLED:
            info->has_status = true;
            info->status = qemu_strdup("cancelled");
            break;
        }
    }

    return info;
}

/* shared migration helpers */

void migrate_fd_monitor_suspend(FdMigrationState *s, Monitor *mon)
{
    s->mon = mon;
    if (monitor_suspend(mon) == 0) {
        DPRINTF("suspending monitor\n");
    } else {
        monitor_printf(mon, "terminal does not allow synchronous "
                       "migration, continuing detached\n");
    }
}

void migrate_fd_error(FdMigrationState *s)
{
    DPRINTF("setting error state\n");
    s->state = MIG_STATE_ERROR;
    migrate_fd_cleanup(s);
}

int migrate_fd_cleanup(FdMigrationState *s)
{
    int ret = 0;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (s->file) {
        DPRINTF("closing file\n");
        if (qemu_fclose(s->file) != 0) {
            ret = -1;
        }
        s->file = NULL;
    }

    if (s->fd != -1)
        close(s->fd);

    /* Don't resume monitor until we've flushed all of the buffers */
    if (s->mon) {
        monitor_resume(s->mon);
    }

    s->fd = -1;

    return ret;
}

void migrate_fd_put_notify(void *opaque)
{
    FdMigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
}

ssize_t migrate_fd_put_buffer(void *opaque, const void *data, size_t size)
{
    FdMigrationState *s = opaque;
    ssize_t ret;

    do {
        ret = s->write(s, data, size);
    } while (ret == -1 && ((s->get_error(s)) == EINTR));

    if (ret == -1)
        ret = -(s->get_error(s));

    if (ret == -EAGAIN) {
        qemu_set_fd_handler2(s->fd, NULL, NULL, migrate_fd_put_notify, s);
    } else if (ret < 0) {
        if (s->mon) {
            monitor_resume(s->mon);
        }
        s->state = MIG_STATE_ERROR;
    }

    return ret;
}

void migrate_fd_connect(FdMigrationState *s)
{
    int ret;

    s->file = qemu_fopen_ops_buffered(s,
                                      s->bandwidth_limit,
                                      migrate_fd_put_buffer,
                                      migrate_fd_put_ready,
                                      migrate_fd_wait_for_unfreeze,
                                      migrate_fd_close);

    DPRINTF("beginning savevm\n");
    ret = qemu_savevm_state_begin(s->mon, s->file, s->mig_state.blk,
                                  s->mig_state.shared);
    if (ret < 0) {
        DPRINTF("failed, %d\n", ret);
        migrate_fd_error(s);
        return;
    }
    
    migrate_fd_put_ready(s);
}

void migrate_fd_put_ready(void *opaque)
{
    FdMigrationState *s = opaque;

    if (s->state != MIG_STATE_ACTIVE) {
        DPRINTF("put_ready returning because of non-active state\n");
        return;
    }

    DPRINTF("iterate\n");
    if (qemu_savevm_state_iterate(s->mon, s->file) == 1) {
        int state;
        int old_vm_running = vm_running;

        DPRINTF("done iterating\n");
        vm_stop(0);

        if ((qemu_savevm_state_complete(s->mon, s->file)) < 0) {
            if (old_vm_running) {
                vm_start();
            }
            state = MIG_STATE_ERROR;
        } else {
            state = MIG_STATE_COMPLETED;
        }
        if (migrate_fd_cleanup(s) < 0) {
            if (old_vm_running) {
                vm_start();
            }
            state = MIG_STATE_ERROR;
        }
        s->state = state;
    }
}

int migrate_fd_get_status(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);
    return s->state;
}

void migrate_fd_cancel(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);

    if (s->state != MIG_STATE_ACTIVE)
        return;

    DPRINTF("cancelling migration\n");

    s->state = MIG_STATE_CANCELLED;
    qemu_savevm_state_cancel(s->mon, s->file);

    migrate_fd_cleanup(s);
}

void migrate_fd_release(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);

    DPRINTF("releasing state\n");
   
    if (s->state == MIG_STATE_ACTIVE) {
        s->state = MIG_STATE_CANCELLED;
        migrate_fd_cleanup(s);
    }
    qemu_free(s);
}

void migrate_fd_wait_for_unfreeze(void *opaque)
{
    FdMigrationState *s = opaque;
    int ret;

    DPRINTF("wait for unfreeze\n");
    if (s->state != MIG_STATE_ACTIVE)
        return;

    do {
        fd_set wfds;

        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);

        ret = select(s->fd + 1, NULL, &wfds, NULL, NULL);
    } while (ret == -1 && (s->get_error(s)) == EINTR);
}

int migrate_fd_close(void *opaque)
{
    FdMigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    return s->close(s);
}
