/*
 * Simple trace backend
 *
 * Copyright IBM, Corp. 2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <signal.h>
#include <pthread.h>
#endif
#include "qemu-timer.h"
#include "trace.h"
#include "trace/control.h"

/** Trace file header event ID */
#define HEADER_EVENT_ID (~(uint64_t)0) /* avoids conflicting with TraceEventIDs */

/** Trace file magic number */
#define HEADER_MAGIC 0xf2b177cb0aa429b4ULL

/** Trace file version number, bump if format changes */
#define HEADER_VERSION 2

/** Records were dropped event ID */
#define DROPPED_EVENT_ID (~(uint64_t)0 - 1)

/** Trace record is valid */
#define TRACE_RECORD_VALID ((uint64_t)1 << 63)

/*
 * Trace records are written out by a dedicated thread.  The thread waits for
 * records to become available, writes them out, and then waits again.
 */
static GStaticMutex trace_lock = G_STATIC_MUTEX_INIT;
static GCond *trace_available_cond;
static GCond *trace_empty_cond;
static bool trace_available;
static bool trace_writeout_enabled;

enum {
    TRACE_BUF_LEN = 4096 * 64,
    TRACE_BUF_FLUSH_THRESHOLD = TRACE_BUF_LEN / 4,
};

uint8_t trace_buf[TRACE_BUF_LEN];
static unsigned int trace_idx;
static unsigned int writeout_idx;
static uint64_t dropped_events;
static FILE *trace_fp;
static char *trace_file_name = NULL;

/* * Trace buffer entry */
typedef struct {
    uint64_t event; /*   TraceEventID */
    uint64_t timestamp_ns;
    uint32_t length;   /*    in bytes */
    uint32_t reserved; /*    unused */
    uint8_t arguments[]; /*  arguments position affects ST_REC_HDR_LEN */
} TraceRecord;

typedef struct {
    uint64_t header_event_id; /* HEADER_EVENT_ID */
    uint64_t header_magic;    /* HEADER_MAGIC    */
    uint64_t header_version;  /* HEADER_VERSION  */
} TraceRecordHeader;

/* * Trace record header length */
#define ST_REC_HDR_LEN 24

int trace_alloc_record(TraceBufferRecord *rec, TraceEventID event, uint32_t datasize);
static void read_from_buffer(unsigned int idx, uint8_t *dataptr, uint32_t size);
static void write_to_buffer(unsigned int idx, uint8_t *dataptr, uint32_t size);
void trace_mark_record_complete(TraceBufferRecord *rec);

uint32_t safe_strlen(const char* str)
{
    if (str == NULL) {
        return 0;
    }
    return strlen(str);
}

/**
 * Read a trace record from the trace buffer
 *
 * @idx         Trace buffer index
 * @record      Trace record to fill
 *
 * Returns false if the record is not valid.
 */
static bool get_trace_record(unsigned int idx, TraceRecord **recordptr)
{
    uint8_t temp_rec[ST_REC_HDR_LEN];
    TraceRecord *record = (TraceRecord *) temp_rec;
    read_from_buffer(idx, temp_rec, ST_REC_HDR_LEN);

    if (!(record->event & TRACE_RECORD_VALID)) {
        return false;
    }

    __sync_synchronize(); /* read memory barrier before accessing record */

    *recordptr = g_malloc(record->length);
    /* make a copy of record to avoid being overwritten */
    read_from_buffer(idx, (uint8_t *)*recordptr, record->length);
    (*recordptr)->event &= ~TRACE_RECORD_VALID;
    return true;
}

/**
 * Kick writeout thread
 *
 * @wait        Whether to wait for writeout thread to complete
 */
static void flush_trace_file(bool wait)
{
    g_static_mutex_lock(&trace_lock);
    trace_available = true;
    g_cond_signal(trace_available_cond);

    if (wait) {
        g_cond_wait(trace_empty_cond, g_static_mutex_get_mutex(&trace_lock));
    }

    g_static_mutex_unlock(&trace_lock);
}

static void wait_for_trace_records_available(void)
{
    g_static_mutex_lock(&trace_lock);
    while (!(trace_available && trace_writeout_enabled)) {
        g_cond_signal(trace_empty_cond);
        g_cond_wait(trace_available_cond,
                    g_static_mutex_get_mutex(&trace_lock));
    }
    trace_available = false;
    g_static_mutex_unlock(&trace_lock);
}

static gpointer writeout_thread(gpointer opaque)
{
    TraceRecord *recordptr;
    unsigned int idx = 0;
    size_t unused __attribute__ ((unused));

    for (;;) {
        wait_for_trace_records_available();

        if (dropped_events) {
            recordptr = g_malloc(ST_REC_HDR_LEN + sizeof(dropped_events));
            recordptr->event = DROPPED_EVENT_ID,
            recordptr->timestamp_ns = get_clock();
            recordptr->length = ST_REC_HDR_LEN + sizeof(dropped_events),
            recordptr->reserved = 0;
            *(uint64_t *) &(recordptr->arguments[0]) = dropped_events;
            dropped_events = 0;
            unused = fwrite(recordptr, recordptr->length, 1, trace_fp);
        }

        while (get_trace_record(idx, &recordptr)) {
            unused = fwrite(recordptr, recordptr->length, 1, trace_fp);
            writeout_idx += recordptr->length;
            g_free(recordptr);
            recordptr = (TraceRecord *) &trace_buf[idx];
            recordptr->event = 0;
            idx = writeout_idx % TRACE_BUF_LEN;
        }

        fflush(trace_fp);
    }
    return NULL;
}

int trace_record_start(TraceBufferRecord *rec, TraceEventID id, size_t arglen)
{
    return trace_alloc_record(rec, id, arglen); /* return 0 on success */
}

void trace_record_write_u64(TraceBufferRecord *rec, uint64_t val)
{
    write_to_buffer(rec->rec_off, (uint8_t *)&val, sizeof(uint64_t));
    rec->rec_off += sizeof(uint64_t);
}

void trace_record_write_str(TraceBufferRecord *rec, const char *s)
{
    /* Write string length first */
    uint32_t slen = (s == NULL ? 0 : strlen(s));
    write_to_buffer(rec->rec_off, (uint8_t *)&slen, sizeof(slen));
    rec->rec_off += sizeof(slen);
    /* Write actual string now */
    write_to_buffer(rec->rec_off, (uint8_t *)s, slen);
    rec->rec_off += slen;
}

void trace_record_finish(TraceBufferRecord *rec)
{
    trace_mark_record_complete(rec);
}

int trace_alloc_record(TraceBufferRecord *rec, TraceEventID event, uint32_t datasize)
{
    unsigned int idx, rec_off;
    uint32_t rec_len = ST_REC_HDR_LEN + datasize;
    uint64_t timestamp_ns = get_clock();

    if ((rec_len + trace_idx - writeout_idx) > TRACE_BUF_LEN) {
        /* Trace Buffer Full, Event dropped ! */
        dropped_events++;
        return 1;
    }
    idx = g_atomic_int_exchange_and_add((gint *)&trace_idx, rec_len) % TRACE_BUF_LEN;

    /*  To check later if threshold crossed */
    rec->next_tbuf_idx = trace_idx % TRACE_BUF_LEN;

    rec_off = idx;
    write_to_buffer(rec_off, (uint8_t*)&event, sizeof(event));
    rec_off += sizeof(event);
    write_to_buffer(rec_off, (uint8_t*)&timestamp_ns, sizeof(timestamp_ns));
    rec_off += sizeof(timestamp_ns);
    write_to_buffer(rec_off, (uint8_t*)&rec_len, sizeof(rec_len));
    rec_off += sizeof(rec_len);

    rec->tbuf_idx = idx;
    rec->rec_off  = (idx + ST_REC_HDR_LEN) % TRACE_BUF_LEN;
    return 0;
}

static void read_from_buffer(unsigned int idx, uint8_t *dataptr, uint32_t size)
{
    uint32_t x = 0;
    while (x < size) {
        if (idx >= TRACE_BUF_LEN) {
            idx = idx % TRACE_BUF_LEN;
        }
        dataptr[x++] = trace_buf[idx++];
    }
}

static void write_to_buffer(unsigned int idx, uint8_t *dataptr, uint32_t size)
{
    uint32_t x = 0;
    while (x < size) {
        if (idx >= TRACE_BUF_LEN) {
            idx = idx % TRACE_BUF_LEN;
        }
        trace_buf[idx++] = dataptr[x++];
    }
}

void trace_mark_record_complete(TraceBufferRecord *rec)
{
    uint8_t temp_rec[ST_REC_HDR_LEN];
    TraceRecord *record = (TraceRecord *) temp_rec;
    read_from_buffer(rec->tbuf_idx, temp_rec, ST_REC_HDR_LEN);
    __sync_synchronize(); /* write barrier before marking as valid */
    record->event |= TRACE_RECORD_VALID;
    write_to_buffer(rec->tbuf_idx, temp_rec, ST_REC_HDR_LEN);

    if (rec->next_tbuf_idx > TRACE_BUF_FLUSH_THRESHOLD &&
        rec->tbuf_idx <= TRACE_BUF_FLUSH_THRESHOLD) {
        flush_trace_file(false);
    }
}



void st_set_trace_file_enabled(bool enable)
{
    if (enable == !!trace_fp) {
        return; /* no change */
    }

    /* Halt trace writeout */
    flush_trace_file(true);
    trace_writeout_enabled = false;
    flush_trace_file(true);

    if (enable) {
        static const TraceRecordHeader header = {
            .header_event_id = HEADER_EVENT_ID,
            .header_magic = HEADER_MAGIC,
            /* Older log readers will check for version at next location */
            .header_version = HEADER_VERSION,
        };

        trace_fp = fopen(trace_file_name, "wb");
        if (!trace_fp) {
            return;
        }

        if (fwrite(&header, sizeof header, 1, trace_fp) != 1) {
            fclose(trace_fp);
            trace_fp = NULL;
            return;
        }

        /* Resume trace writeout */
        trace_writeout_enabled = true;
        flush_trace_file(false);
    } else {
        fclose(trace_fp);
        trace_fp = NULL;
    }
}

/**
 * Set the name of a trace file
 *
 * @file        The trace file name or NULL for the default name-<pid> set at
 *              config time
 */
bool st_set_trace_file(const char *file)
{
    st_set_trace_file_enabled(false);

    free(trace_file_name);

    if (!file) {
        if (asprintf(&trace_file_name, CONFIG_TRACE_FILE, getpid()) < 0) {
            trace_file_name = NULL;
            return false;
        }
    } else {
        if (asprintf(&trace_file_name, "%s", file) < 0) {
            trace_file_name = NULL;
            return false;
        }
    }

    st_set_trace_file_enabled(true);
    return true;
}

void st_print_trace_file_status(FILE *stream, int (*stream_printf)(FILE *stream, const char *fmt, ...))
{
    stream_printf(stream, "Trace file \"%s\" %s.\n",
                  trace_file_name, trace_fp ? "on" : "off");
}


void st_flush_trace_buffer(void)
{
    flush_trace_file(true);
}

void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    unsigned int i;

    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_list[i].tp_name, i, trace_list[i].state);
    }
}

bool trace_event_set_state(const char *name, bool state)
{
    unsigned int i;
    unsigned int len;
    bool wildcard = false;
    bool matched = false;

    len = strlen(name);
    if (len > 0 && name[len - 1] == '*') {
        wildcard = true;
        len -= 1;
    }
    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        if (wildcard) {
            if (!strncmp(trace_list[i].tp_name, name, len)) {
                trace_list[i].state = state;
                matched = true;
            }
            continue;
        }
        if (!strcmp(trace_list[i].tp_name, name)) {
            trace_list[i].state = state;
            return true;
        }
    }
    return matched;
}

/* Helper function to create a thread with signals blocked.  Use glib's
 * portable threads since QEMU abstractions cannot be used due to reentrancy in
 * the tracer.  Also note the signal masking on POSIX hosts so that the thread
 * does not steal signals when the rest of the program wants them blocked.
 */
static GThread *trace_thread_create(GThreadFunc fn)
{
    GThread *thread;
#ifndef _WIN32
    sigset_t set, oldset;

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);
#endif
    thread = g_thread_create(fn, NULL, FALSE, NULL);
#ifndef _WIN32
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif

    return thread;
}

bool trace_backend_init(const char *events, const char *file)
{
    GThread *thread;

    if (!g_thread_supported()) {
#if !GLIB_CHECK_VERSION(2, 31, 0)
        g_thread_init(NULL);
#else
        fprintf(stderr, "glib threading failed to initialize.\n");
        exit(1);
#endif
    }

    trace_available_cond = g_cond_new();
    trace_empty_cond = g_cond_new();

    thread = trace_thread_create(writeout_thread);
    if (!thread) {
        fprintf(stderr, "warning: unable to initialize simple trace backend\n");
        return false;
    }

    atexit(st_flush_trace_buffer);
    trace_backend_init_events(events);
    st_set_trace_file(file);
    return true;
}
