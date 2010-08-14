/*
 * Virtio Network Device
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_QPI_MIGRATION_H
#define QEMU_QPI_MIGRATION_H

#include "error.h"
#include "migration.h"

/* Live Migration API implementation.
 *
 * Live migration is a background task that attempts to move a guest to another
 * QEMU instance minimizing the downtime of the guest.
 *
 * Migration convergence cannot be guaranteed since it depends on a guest load
 * and available network bandwidth.  This means that the migration process could
 * never finish.
 *
 * An application should monitor the duration of the live migration session and
 * monitor the progress.  If the application detects that the migration is
 * taking an unreasonable amount of time, it should either cancel the migration
 * or increase the likelihood of convergence by increasing the bandwidth limit
 * and/or increasing the allowed down time.
 *
 * Migration convergence can be forced by stopping the guest.  This will result
 * in prolonged guest down time based on the amount of network bandwidth
 * available.
 *
 * A reasonable implementation could be waiting a fixed amount of time (say 600
 * seconds) and checking to see how much progress has been made.  If the memory
 * has been transferred at least 3 full times, the guest should be stopped to
 * force migration.
 */

/* Tracks the phases of migration.  The following transitions are valid:
 *
 *  INACTIVE ->
 *    ACTIVE ->
 *      FAILED
 *      CANCELLED
 *      COMPLETE
 */
typedef enum MigrationPhase
{
    MP_INACTIVE,   /* initial state */
    MP_ACTIVE,     /* migration has been activated */
    MP_FAILED,     /* migration failed */
    MP_CANCELLED,  /* migration cancelled */
    MP_COMPLETE,   /* migration completed successfully */
} MigrationPhase;

/* Create a new migration context */
MigrationState *qpi_migration_create(Error **err);

/* Release a migration context.  If phase == ACTIVE, cancel the migration first
 * before releasing.
 */
void qpi_migration_release(MigrationState *s,
                           Error **err);

/* Set the URI of the migration session.  This is only valid
 * if phase == INACTIVE.
 */
void qpi_migration_set_uri(MigrationState *s,
                           const char *uri,
                           Error **err);

/* Get the URI of the migration session.  This is valid in any phase. */
const char *qpi_migration_get_uri(MigrationState *s,
                                  Error **err);

/* Set the bandwidth rate limit for the migration session.  This is valid when
 * phase == INACTIVE or phase == ACTIVE.  A value of bytes_per_sec of zero
 * indicates that session should not be rate limited.  The default value is
 * zero.
 */
void qpi_migration_set_rate_limit(MigrationState *s,
                                  uint32_t bytes_per_sec,
                                  Error **err);

/* Get the bandwidth rate limit for the migration session.  This is valid in
 * any phase.
 */
uint32_t qpi_migration_get_rate_limit(MigrationState *s,
                                      Error **err);

/* Set the target down time for the live migration.  This value should typically
 * be set to a reasonably small value to avoid disrupting applications or
 * triggering network timeouts.  The value is in nanoseconds.
 *
 * Since migration convergence depends on the guest's workload and available
 * bandwidth, it may not be possible to complete the migration according to the
 * request down time.
 *
 * The default down time is 30ms or 30,000,000 ns.
 */
void qpi_migration_set_down_time(MigrationState *s,
                                 uint64_t ns,
                                 Error **err);

/* Get the target down time (in nanoseconds) */
uint64_t qpi_migration_get_down_time(MigrationState *s,
                                     Error **err);

/* Get the current migration phase.  N.B. by the time this function returns
 * a phase of ACTIVE, it's possible the migration has already moved into a
 * different phase.  Whenever using a function that's only during the ACTIVE
 * phase, you must gracefully handle errors from an invalid state. */
MigrationPhase qpi_migration_get_phase(MigrationState *s,
                                       Error **err);

/* Get the number of bytes written so far.  This is valid in any phase*/
uint64_t qpi_migration_get_bytes_transferred(MigrationState *s,
                                             Error **err);

/* Get an estimate of the number of bytes remaining to be written.  This is
 * not a guaranteed amount and should be treated as informational. This returns
 * 0 when not in the ACTIVE state.  In the INACTIVE state, it returns
 * bytes_total.
 */
uint64_t qpi_migration_get_bytes_remaining(MigrationState *s,
                                           Error **err);

/* Get an estimate of the total number of bytes needed to send the migration
 * traffic.  This may not be bytes_transferred + bytes_remaining due to the
 * fact that bytes_remaining is an unstable value.
 */
uint64_t qpi_migration_get_bytes_total(MigrationState *s,
                                       Error **err);

/* Get the error has occurred during the migration session.  Since migration
 * is a background task, an error can occur without any single command being
 * responsible for the error.  This is only valid when phase == FAILED.
 */
Error *qpi_migration_get_error(MigrationState *s,
                               Error **err);

/* Add a notifier for phase changes.  The notifier fires after the phase
 * changes.
 */
void qpi_migration_add_phase_change_notifier(MigrationState *s,
                                             Notifier *n,
                                             Error *err);

/* Removes a phase change notifier. */
void qpi_migration_del_phase_change_notifier(MigrationState *s,
                                             Notifier *n,
                                             Error *err);

/* Start a migration.  The URI must be set prior to calling this function.  This
 * can only be called when phase is INACTIVE and will cause a transition to the
 * ACTIVE phase. */
void qpi_migration_start(MigrationState *s,
                         Error **err);

/* Cancel a migration.  This must be called when phase is ACTIVE and can
 * potentially fail because a migration has completed or failed since the last
 * time the phase had been read. */
void qpi_migration_cancel(MigrationState *s,
                          Error **err);

#endif
