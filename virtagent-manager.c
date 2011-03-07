/*
 * virtagent - job queue management
 *
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "virtagent-common.h"

typedef struct VAServerJob {
    char tag[64];
    void *opaque;
    VAServerJobOps ops;
    QTAILQ_ENTRY(VAServerJob) next;
    enum {
        VA_SERVER_JOB_STATE_NEW = 0,
        VA_SERVER_JOB_STATE_BUSY,
        VA_SERVER_JOB_STATE_EXECUTED,
        VA_SERVER_JOB_STATE_SENT,
        VA_SERVER_JOB_STATE_DONE,
    } state;
} VAServerJob;

typedef struct VAClientJob {
    char tag[64];
    void *opaque;
    void *resp_opaque;
    VAClientJobOps ops;
    QTAILQ_ENTRY(VAClientJob) next;
    enum {
        VA_CLIENT_JOB_STATE_NEW = 0,
        VA_CLIENT_JOB_STATE_BUSY,
        VA_CLIENT_JOB_STATE_SENT,
        VA_CLIENT_JOB_STATE_READ,
        VA_CLIENT_JOB_STATE_DONE,
    } state;
} VAClientJob;

#define SEND_COUNT_MAX 1
#define EXECUTE_COUNT_MAX 4

struct VAManager {
    int send_count; /* sends in flight */
    int execute_count; /* number of jobs currently executing */
    QTAILQ_HEAD(, VAServerJob) server_jobs;
    QTAILQ_HEAD(, VAClientJob) client_jobs;
};

/* server job operations/helpers */

static VAServerJob *va_server_job_by_tag(VAManager *m, const char *tag)
{
    VAServerJob *j;
    QTAILQ_FOREACH(j, &m->server_jobs, next) {
        if (strcmp(j->tag, tag) == 0) {
            return j;
        }
    }
    return NULL;
}

int va_server_job_add(VAManager *m, const char *tag, void *opaque,
                      VAServerJobOps ops)
{
    VAServerJob *j = qemu_mallocz(sizeof(VAServerJob));
    TRACE("called");
    j->state = VA_SERVER_JOB_STATE_NEW;
    j->ops = ops;
    j->opaque = opaque;
    memset(j->tag, 0, 64);
    pstrcpy(j->tag, 63, tag);
    QTAILQ_INSERT_TAIL(&m->server_jobs, j, next);
    va_kick(m);
    return 0;
}

static void va_server_job_execute(VAServerJob *j)
{
    TRACE("called");
    j->state = VA_SERVER_JOB_STATE_BUSY;
    j->ops.execute(j->opaque, j->tag);
}

/* TODO: need a way to pass information back */
void va_server_job_execute_done(VAManager *m, const char *tag)
{
    VAServerJob *j = va_server_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("server job with tag \"%s\" not found", tag);
        return;
    }
    j->state = VA_SERVER_JOB_STATE_EXECUTED;
    va_kick(m);
}

static void va_server_job_send(VAServerJob *j)
{
    TRACE("called");
    j->state = VA_SERVER_JOB_STATE_BUSY;
    j->ops.send(j->opaque, j->tag);
}

void va_server_job_send_done(VAManager *m, const char *tag)
{
    VAServerJob *j = va_server_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("server job with tag \"%s\" not found", tag);
        return;
    }
    j->state = VA_SERVER_JOB_STATE_SENT;
    m->send_count--;
    va_kick(m);
}

static void va_server_job_callback(VAServerJob *j)
{
    TRACE("called");
    j->state = VA_SERVER_JOB_STATE_BUSY;
    if (j->ops.callback) {
        j->ops.callback(j->opaque, j->tag);
    }
    j->state = VA_SERVER_JOB_STATE_DONE;
}

void va_server_job_cancel(VAManager *m, const char *tag)
{
    VAServerJob *j = va_server_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("server job with tag \"%s\" not found", tag);
        return;
    }
    /* TODO: need to decrement sends/execs in flight appropriately */
    /* make callback and move to done state, kick() will handle cleanup */
    va_server_job_callback(j);
    va_kick(m);
}

/* client job operations */

static VAClientJob *va_client_job_by_tag(VAManager *m, const char *tag)
{
    VAClientJob *j;
    QTAILQ_FOREACH(j, &m->client_jobs, next) {
        if (strcmp(j->tag, tag) == 0) {
            return j;
        }
    }
    return NULL;
}

int va_client_job_add(VAManager *m, const char *tag, void *opaque,
                      VAClientJobOps ops)
{
    VAClientJob *j = qemu_mallocz(sizeof(VAClientJob));
    TRACE("called");
    j->ops = ops;
    j->opaque = opaque;
    memset(j->tag, 0, 64);
    pstrcpy(j->tag, 63, tag);
    QTAILQ_INSERT_TAIL(&m->client_jobs, j, next);
    va_kick(m);
    return 0;
}

static void va_client_job_send(VAClientJob *j)
{
    TRACE("called");
    j->state = VA_CLIENT_JOB_STATE_BUSY;
    j->ops.send(j->opaque, j->tag);
}

void va_client_job_send_done(VAManager *m, const char *tag)
{
    VAClientJob *j = va_client_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("client job with tag \"%s\" not found", tag);
        return;
    }
    j->state = VA_CLIENT_JOB_STATE_SENT;
    m->send_count--;
    va_kick(m);
}

void va_client_job_read_done(VAManager *m, const char *tag, void *resp)
{
    VAClientJob *j = va_client_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("client job with tag \"%s\" not found", tag);
        return;
    }
    j->state = VA_CLIENT_JOB_STATE_READ;
    j->resp_opaque = resp;
    va_kick(m);
}

static void va_client_job_callback(VAClientJob *j)
{
    TRACE("called");
    j->state = VA_CLIENT_JOB_STATE_BUSY;
    if (j->ops.callback) {
        j->ops.callback(j->opaque, j->resp_opaque, j->tag);
    }
    j->state = VA_CLIENT_JOB_STATE_DONE;
}

void va_client_job_cancel(VAManager *m, const char *tag)
{
    VAClientJob *j = va_client_job_by_tag(m, tag);
    TRACE("called");
    if (!j) {
        LOG("client job with tag \"%s\" not found", tag);
        return;
    }
    /* TODO: need to decrement sends/execs in flight appropriately */
    /* make callback and move to done state, kick() will handle cleanup */
    va_client_job_callback(j);
    va_kick(m);
}

/* general management functions */

VAManager *va_manager_new(void)
{
    VAManager *m = qemu_mallocz(sizeof(VAManager));
    QTAILQ_INIT(&m->client_jobs);
    QTAILQ_INIT(&m->server_jobs);
    return m;
}

static void va_process_server_job(VAManager *m, VAServerJob *sj)
{
    switch (sj->state) {
        case VA_SERVER_JOB_STATE_NEW:
            TRACE("marker");
            va_server_job_execute(sj);
            break;
        case VA_SERVER_JOB_STATE_EXECUTED:
            TRACE("marker");
            if (m->send_count < SEND_COUNT_MAX) {
                TRACE("marker");
                va_server_job_send(sj);
                m->send_count++;
            }
            break;
        case VA_SERVER_JOB_STATE_SENT:
            TRACE("marker");
            va_server_job_callback(sj);
            break;
        case VA_SERVER_JOB_STATE_BUSY:
            TRACE("marker, server job currently busy");
            break;
        default:
            LOG("error, unknown server job state");
            break;
    }
}

static void va_process_client_job(VAManager *m, VAClientJob *cj)
{
    switch (cj->state) {
        case VA_CLIENT_JOB_STATE_NEW:
            TRACE("marker");
            if (m->send_count < SEND_COUNT_MAX) {
                TRACE("marker");
                va_client_job_send(cj);
                m->send_count++;
            }
            break;
        case VA_CLIENT_JOB_STATE_SENT:
            TRACE("marker");
            //nothing to do here, awaiting read_done()
            break;
        case VA_CLIENT_JOB_STATE_READ:
            TRACE("marker");
            va_client_job_callback(cj);
            break;
        case VA_CLIENT_JOB_STATE_DONE:
            TRACE("marker");
            QTAILQ_REMOVE(&m->client_jobs, cj, next);
        case VA_CLIENT_JOB_STATE_BUSY:
            TRACE("marker, client job currently busy");
            break;
        default:
            LOG("error, unknown client job state");
            break;
    }
}

void va_kick(VAManager *m)
{
    VAServerJob *sj, *sj_tmp;
    VAClientJob *cj, *cj_tmp;

    TRACE("called");
    TRACE("send_count: %u, execute_count: %u", m->send_count, m->execute_count);

    /* TODO: make sure there is no starvation of jobs/operations here */

    /* look for any work to be done among pending server jobs */
    QTAILQ_FOREACH_SAFE(sj, &m->server_jobs, next, sj_tmp) {
        TRACE("marker, server tag: %s", sj->tag);
        va_process_server_job(m, sj);
    }

    /* look for work to be done among pending client jobs */
    QTAILQ_FOREACH_SAFE(cj, &m->client_jobs, next, cj_tmp) {
        TRACE("marker, client tag: %s", cj->tag);
        va_process_client_job(m, cj);
    }
}
