#ifndef QPI_MIGRATION_H
#define QPI_MIGRATION_H

#include "migration.h"

typedef struct MigrationParameters
{
    uint64_t max_bytes_per_sec;
    uint64_t max_downtime_ns;
} MigrationParameters;

typedef enum MigrationState
{
    MGS_ACTIVE = 0,
    MGS_CANCELLED = 1,
    MGS_FAILED = 2,
    MGS_COMPLETED = 3,
} MigrationState;

typedef struct MigrationStats
{
    MigrationState state;

    /* only valid if state == MGS_ACTIVE */
    uint64_t bytes_transferred;  
    uint64_t bytes_remaining;
    uint64_t bytes_total;

    /* only valid if state == MGS_FAILED */
    Error *error;
} MigrationStats;

MigrationState *qpi_migrate_create(const char *uri,
                                   const MigrationParameters *params,
                                   Error **error);

void qpi_migrate_set_params(MigrationState *s,
                            const MigrationParameters *params,
                            Error **error);

void qpi_migrate_get_params(MigrationState *s,
                            MigrationParameters *params,
                            Error **error);

void qpi_migrate_get_stats(MigrationState *s,
                           MigrationStats *stats,
                           Error **error);

void qpi_migrate_cancel(MigrationState *s,
                        Error **error);

void qpi_migrate_release(MigrationState *s,
                         Error **error);

#endif
