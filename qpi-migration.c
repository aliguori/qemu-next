/*
 * QEMU Live Migration QPI Interface
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

#include "qpi-migration.h"

MigrationState *qpi_migration_create(Error **err)
{
   
}

void qpi_migration_release(MigrationState *s,
                           Error **err)
{
}

void qpi_migration_set_uri(MigrationState *s,
                           const char *uri,
                           Error **err)
{
}

const char *qpi_migration_get_uri(MigrationState *s,
                                  Error **err)
{
}

void qpi_migration_set_rate_limit(MigrationState *s,
                                  uint32_t bytes_per_sec,
                                  Error **err)
{
}

uint32_t qpi_migration_get_rate_limit(MigrationState *s,
                                      Error **err)
{
}

void qpi_migration_set_down_time(MigrationState *s,
                                 uint64_t ns,
                                 Error **err)

uint64_t qpi_migration_get_down_time(MigrationState *s,
                                     Error **err)
{
}

MigrationPhase qpi_migration_get_phase(MigrationState *s,
                                       Error **err)
{
}

uint64_t qpi_migration_get_bytes_transferred(MigrationState *s,
                                             Error **err)
{
}

uint64_t qpi_migration_get_bytes_remaining(MigrationState *s,
                                           Error **err)
{
}

uint64_t qpi_migration_get_bytes_total(MigrationState *s,
                                       Error **err)
{
}

Error *qpi_migration_get_error(MigrationState *s,
                               Error **err)
{
}

void qpi_migration_add_phase_change_notifier(MigrationState *s,
                                             Notifier *n,
                                             Error *err)
{
}

void qpi_migration_del_phase_change_notifier(MigrationState *s,
                                             Notifier *n,
                                             Error *err)
{
}

void qpi_migration_start(MigrationState *s,
                         Error **err)
{
}

void qpi_migration_cancel(MigrationState *s,
                          Error **err)
{
}
