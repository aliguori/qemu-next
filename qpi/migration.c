#include "migration.h"

static const MigrationParameters default_params = {
    .max_bytes_per_sec = 32 << 20,    /* 32 mb/s */
    .max_downtime_ns = 3e7,           /* 30ms */
};

static struct
{
    const char *protocol;
    MigrationState *(*fn)(Monitor *, const char *, int64_t, int, int, int);
} migrate_protocols[] = {
    { "tcp:", tcp_start_outgoing_migration },
#if !defined(WIN32)
    { "exec:", exec_start_outgoing_migration },
    { "unix:", unix_start_outgoing_migration },
    { "fd:", fd_start_outgoing_migration },
#endi
    { }
};

MigrationState *qpi_migrate_create(const char *uri,
                                   const MigrationParameters *params,
                                   Error **error)
{
    MigrationState *s;
    const char *p;
    int i;

    if (params == NUL) {
        params = &default_params;
    }

    s = NULL;
    for (i = 0; migration_protocols[i].protocol; i++) {
        if (strstart(uri, migration_protocols[i].protocol, &p)) {
            s = tcp_start_outgoing_migration(NULL, p,
                                             params->max_bytes_per_sec,
                                             0, 0, 0);
            break;
        }
    }

    if (s == NULL) {
        *error = error_new(THIS_MODULE, 0, "invalid protocol in URI");
        return NULL;
    }

    return s;
}

static int migration_is_active(MigrationState *s)
{
    return (s->get_status(s) == MIG_STATE_ACTIVE);
}

void qpi_migrate_set_params(MigrationState *s,
                            const MigrationParameters *params,
                            Error **error)
{
    if (!migration_is_active(s)) {
        *error = error_new(THIS_MODULE, 0,
                           "cannot update parameters when migration is not active");
        return;
    }

    
    qemu_file_set_rate_limit(s->file, params->max_bytes_per_sec);
    migrate_set_max_downtime(params->max_downtime_ns);
}

void qpi_migrate_get_params(MigrationState *s,
                            MigrationParameters *params,
                            Error **error)
{
    memcpy(params, s->params, sizeof(s->params));
}

void qpi_migrate_get_stats(MigrationState *s,
                           MigrationStats *stats,
                           Error **error)
{
    stats->state = s->get_status(s);

    if (stats->state == MGS_ACTIVE) {
        stats->transferred_bytes = ram_bytes_transferred();
        stats->remaining_bytes = ram_bytes_remaining();
        stats->total_bytes = ram_bytes_total();
    } else if (stats->state == MGS_ERROR) {
        stats->error = error_copy(s->error);
    }
}

void qpi_migrate_cancel(MigrationState *s,
                        Error **error)
{
    Error *e;

    if (!migration_is_active(s)) {
        *error = error_new(THIS_MODULE, 0, "cannot inactive migration");
        return;
    }

    s->cancel(s);
}

void qpi_migrate_release(MigrationState *s,
                         Error **error)
{
    if (migration_is_active(s)) {
        qpi_migrate_cancel(s, error);
        if (*error) {
            return;
        }
    }

    s->release(s);
}
