/*
 * virtagent - common host/guest RPC functions
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Adam Litke        <aglitke@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "virtagent-common.h"

typedef struct VAClientJob {
    char client_tag[64];
    xmlrpc_mem_block *req_data;
    char *resp_data;
    size_t resp_data_len;
    VAClientCallback *cb;
    QTAILQ_ENTRY(VAClientJob) next;
    /* for use by QMP functions */
    MonitorCompletion *mon_cb;
    void *mon_data;
} VAClientJob;

typedef struct VAServerJob {
    char client_tag[64];
    xmlrpc_mem_block *resp_data;
    char *req_data;
    size_t req_data_len;
    void *opaque;
    QTAILQ_ENTRY(VAServerJob) next;
} VAServerJob;

enum va_http_status {
    VA_HTTP_STATUS_NEW,
    VA_HTTP_STATUS_OK,
    VA_HTTP_STATUS_ERROR,
};

enum va_http_type {
    VA_HTTP_TYPE_UNKNOWN = 1,
    VA_HTTP_TYPE_REQUEST,
    VA_HTTP_TYPE_RESPONSE,
} va_http_type;

typedef void (VAHTSendCallback)(enum va_http_status http_status,
                                const char *content, size_t content_len);
typedef void (VAHTReadCallback)(enum va_http_status http_status,
                                const char *content, size_t content_len,
                                const char client_tag[64],
                                enum va_http_type http_type);
typedef struct VAHTState {
    enum {
        VA_SEND_START,
        VA_SEND_HDR,
        VA_SEND_BODY,
        VA_READ_START,
        VA_READ_HDR,
        VA_READ_BODY,
    } state;
    char hdr[VA_HDR_LEN_MAX];
    char hdr_client_tag[64];
    size_t hdr_len;
    size_t hdr_pos;
    char *content;
    size_t content_len;
    size_t content_pos;
    VAHTSendCallback *send_cb;
    VAHTReadCallback *read_cb;
    enum va_http_type http_type;
} VAHTState;

typedef struct VAState {
    bool is_host;
    const char *channel_method;
    const char *channel_path;
    int fd;
    enum va_client_state {
        VA_CLIENT_IDLE = 0,
        VA_CLIENT_SEND,     /* sending rpc request */
        VA_CLIENT_WAIT,     /* waiting for rpc response */
    } client_state;
    enum va_server_state {
        VA_SERVER_IDLE = 0,
        VA_SERVER_WAIT,     /* waiting to send rpc response */
        VA_SERVER_SEND,     /* sending rpc response */
    } server_state;
    VAClientData client_data;
    VAServerData server_data;
    int client_job_count;
    QTAILQ_HEAD(, VAClientJob) client_jobs;
    int server_job_count;
    QTAILQ_HEAD(, VAServerJob) server_jobs;
    /* for use by async send/read handlers for fd */
    VAHTState send_state;
    VAHTState read_state;
} VAState;

static VAState *va_state;

static bool va_set_client_state(enum va_client_state client_state);
static VAServerJob *va_pop_server_job(void);
static VAClientJob *va_pop_client_job(void);
static int va_kick(void);
static int va_connect(void);
static void va_http_read_handler(void *opaque);
static void va_http_read_handler_reset(void);

static VAClientJob *va_current_client_job(void)
{
    TRACE("called");
    return QTAILQ_FIRST(&va_state->client_jobs);
}

static void va_cancel_jobs(void)
{
    VAClientJob *cj, *cj_tmp;
    VAServerJob *sj, *sj_tmp;

    TRACE("called");
    /* reset read handler, and cancel any current sends */
    va_http_read_handler_reset();
    qemu_set_fd_handler(va_state->fd, va_http_read_handler, NULL, NULL);

    /* cancel/remove any queued client jobs */
    QTAILQ_FOREACH_SAFE(cj, &va_state->client_jobs, next, cj_tmp) {
        /* issue cb with failure notification */
        cj->cb(NULL, 0, cj->mon_cb, cj->mon_data);
        QTAILQ_REMOVE(&va_state->client_jobs, cj, next);
    }
    va_state->client_job_count = 0;

    /* cancel/remove any queued server jobs */
    QTAILQ_FOREACH_SAFE(sj, &va_state->server_jobs, next, sj_tmp) {
        QTAILQ_REMOVE(&va_state->server_jobs, sj, next);
    }
    va_state->server_job_count = 0;

    va_state->client_state = VA_CLIENT_IDLE;
    va_state->server_state = VA_SERVER_IDLE;
}

/***********************************************************/
/* callbacks for read/send handlers */

static void va_client_send_cb(enum va_http_status http_status,
                              const char *content, size_t content_len)
{
    VAClientJob *client_job = va_current_client_job();

    TRACE("called");
    assert(client_job != NULL);

    if (http_status != VA_HTTP_STATUS_OK) {
        /* TODO: we should reset everything at this point...guest/host will
         * be out of whack with each other since there's no way to let the
         * other know job failed (server or client job) if the send channel
         * is down. But how do we induce the other side to do the same?
         */
        LOG("error sending http request");
    }

    /* request sent ok. free up request xml, then move to
     * wait (for response) state
     */
    XMLRPC_MEMBLOCK_FREE(char, client_job->req_data);
    assert(va_set_client_state(VA_CLIENT_WAIT));
}

static void va_server_send_cb(enum va_http_status http_status,
                              const char *content, size_t content_len)
{
    VAServerJob *server_job = va_pop_server_job();

    TRACE("called");
    assert(server_job != NULL);

    if (http_status != VA_HTTP_STATUS_OK) {
        /* TODO: we should reset everything at this point...guest/host will
         * be out of whack with each other since there's no way to let the
         * other know job failed (server or client job) if the send channel
         * is down
         */
        LOG("error sending http response");
        return;
    }

    /* response sent ok, cleanup server job and kick off the next one */
    XMLRPC_MEMBLOCK_FREE(char, server_job->resp_data);
    qemu_free(server_job);
    va_kick();
}

static void va_client_read_cb(const char *content, size_t content_len,
                              const char client_tag[64])
{
    VAClientJob *client_job;

    TRACE("called");
    client_job = va_pop_client_job();
    assert(client_job != NULL);
    if (strncmp(client_job->client_tag, client_tag, 64)) {
        LOG("http client tag mismatch");
    } else {
        TRACE("tag matched: %s", client_tag);
    }
    client_job->cb(content, content_len, client_job->mon_cb,
                   client_job->mon_data);
    va_kick();
}

static void va_server_read_cb(const char *content, size_t content_len,
                              const char client_tag[64])
{
    int ret;

    TRACE("called");
    /* generate response and queue it up for sending */
    ret = va_do_server_rpc(content, content_len, client_tag);
    if (ret != 0) {
        LOG("error creating handling remote rpc request: %s", strerror(ret));
    }

    return;
}

static void va_http_read_cb(enum va_http_status http_status,
                            const char *content, size_t content_len,
                            const char client_tag[64],
                            enum va_http_type http_type)
{
    TRACE("called");
    if (http_status != VA_HTTP_STATUS_OK) {
        LOG("error reading http stream (type %d)", http_type);
        va_cancel_jobs();
        return;
    }

    if (http_type == VA_HTTP_TYPE_REQUEST) {
        TRACE("read request: %s", content);
        va_server_read_cb(content, content_len, client_tag);
    } else if (http_type == VA_HTTP_TYPE_RESPONSE) {
        TRACE("read response: %s", content);
        va_client_read_cb(content, content_len, client_tag);
    } else {
        LOG("unknown http response/request type");
        va_cancel_jobs();
    }

    return;
}

/***********************************************************/
/* utility functions for handling http calls */

static void va_http_hdr_init(VAHTState *s, enum va_http_type http_type) {
    const char *preamble;

    TRACE("called");
    /* essentially ignored in the context of virtagent, but might as well */
    if (http_type == VA_HTTP_TYPE_REQUEST) {
        preamble = "POST /RPC2 HTTP/1.1";
    } else if (http_type == VA_HTTP_TYPE_RESPONSE) {
        preamble = "HTTP/1.1 200 OK";
    } else {
        s->hdr_len = 0;
        return;
    }
    memset(s->hdr, 0, VA_HDR_LEN_MAX);
    s->hdr_len = sprintf(s->hdr,
                         "%s" EOL
                         "Content-Type: text/xml" EOL
                         "Content-Length: %u" EOL
                         "X-Virtagent-Client-Tag: %s" EOL EOL,
                         preamble,
                         (uint32_t)s->content_len,
                         s->hdr_client_tag[0] ? s->hdr_client_tag : "none");
}

#define VA_LINE_LEN_MAX 1024
static void va_rpc_parse_hdr(VAHTState *s)
{
    int i, line_pos = 0;
    bool first_line = true;
    char line_buf[VA_LINE_LEN_MAX];

    TRACE("called");

    for (i = 0; i < VA_HDR_LEN_MAX; ++i) {
        if (s->hdr[i] == 0) {
            /* end of header */
            return;
        }
        if (s->hdr[i] != '\n') {
            /* read line */
            line_buf[line_pos++] = s->hdr[i];
        } else {
            /* process line */
            if (first_line) {
                if (strncmp(line_buf, "POST", 4) == 0) {
                    s->http_type = VA_HTTP_TYPE_REQUEST;
                } else if (strncmp(line_buf, "HTTP", 4) == 0) {
                    s->http_type = VA_HTTP_TYPE_RESPONSE;
                } else {
                    s->http_type = VA_HTTP_TYPE_UNKNOWN;
                }
                first_line = false;
            }
            if (strncmp(line_buf, "Content-Length: ", 16) == 0) {
                s->content_len = atoi(&line_buf[16]);
            }
            if (strncmp(line_buf, "X-Virtagent-Client-Tag: ", 24) == 0) {
                memcpy(s->hdr_client_tag, &line_buf[24], MIN(line_pos-25, 64));
                //pstrcpy(s->hdr_client_tag, 64, &line_buf[24]);
                TRACE("\nTAG<%s>\n", s->hdr_client_tag);
            }
            line_pos = 0;
            memset(line_buf, 0, VA_LINE_LEN_MAX);
        }
    }
}

static int va_start_of_header(char buf[8], int cur_pos)
{
    if (buf[(cur_pos - 4) % 8] == 'H' &&
        buf[(cur_pos - 3) % 8] == 'T' &&
        buf[(cur_pos - 2) % 8] == 'T' &&
        buf[(cur_pos - 1) % 8] == 'P') {
        return 4;
    }

    if (buf[(cur_pos - 4) % 8] == 'P' &&
        buf[(cur_pos - 3) % 8] == 'O' &&
        buf[(cur_pos - 2) % 8] == 'S' &&
        buf[(cur_pos - 1) % 8] == 'T') {
        return 4;
    }

    return -1;
}

static int va_end_of_header(char *buf, int end_pos)
{
    return !strncmp(buf+(end_pos-2), "\n\r\n", 3);
}

static void va_http_read_handler_reset(void)
{
    VAHTState *s = &va_state->read_state;
    TRACE("called");
    s->state = VA_READ_START;
    s->http_type = VA_HTTP_TYPE_UNKNOWN;
    s->hdr_pos = 0;
    s->content_len = 0;
    s->content_pos = 0;
    strcpy(s->hdr_client_tag, "none");
    if (s->content != NULL) {
        qemu_free(s->content);
    }
    s->content = NULL;
}

/***********************************************************/
/* read/send handlers */

static void va_http_read_handler(void *opaque)
{
    VAHTState *s = &va_state->read_state;
    enum va_http_status http_status;
    int fd = va_state->fd;
    int ret, tmp_len, i;
    static char tmp[8];
    static int tmp_pos = 0;
    static int bytes_skipped = 0;

    TRACE("called with opaque: %p", opaque);

    /* until timeouts are implemented, make sure we kick so any deferred
     * jobs get a chance to run
     */
    va_kick();

    switch (s->state) {
    case VA_READ_START:
        /* we may have gotten here due to a http error, indicating
         * a potential unclean state where we are not 'aligned' on http
         * boundaries. we should read till we hit the next http preamble
         * rather than assume we're at the start of an http header
         */
        while((ret = read(fd, tmp + (tmp_pos % 8), 1)) > 0) {
            tmp_pos += ret;
            bytes_skipped += ret;
            if ((tmp_len = va_start_of_header(tmp, tmp_pos)) > 0) {
                bytes_skipped -= tmp_len;
                break;
            }
        }
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            } else {
                LOG("error reading connection: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_reconnect;
        } else {
            /* we've found the start of the header, copy what we've read
             * into hdr buf and set pos accordingly */
            for (i = 0; i < tmp_len; i++) {
                s->hdr[i] = tmp[(tmp_pos % 8) - (tmp_len - i)];
            }
            s->hdr_pos = tmp_len;
            TRACE("read http header start:\n<<<%s>>>\n", s->hdr);
            TRACE("current http header pos: %d", (int)s->hdr_pos);
            TRACE("number of bytes skipped: %d", bytes_skipped);
            tmp_pos = 0;
            memset(tmp, 0, 8);
            bytes_skipped = 0;
            s->state = VA_READ_HDR;
        }
    case VA_READ_HDR:
        while((ret = read(fd, s->hdr + s->hdr_pos, 1)) > 0
              && s->hdr_pos < VA_HDR_LEN_MAX) {
            s->hdr_pos += ret;
            if (va_end_of_header(s->hdr, s->hdr_pos - 1)) {
                break;
            }
        }
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            } else {
                LOG("error reading connection: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_reconnect;
        } else if (s->hdr_pos >= VA_HDR_LEN_MAX) {
            LOG("http header too long");
            goto out_bad;
        } else {
            s->content_len = -1;
            va_rpc_parse_hdr(s);
            if (s->content_len == -1) {
                LOG("malformed http header");
                goto out_bad;
            } else if (s->content_len > VA_CONTENT_LEN_MAX) {
                LOG("http content length too long");
                goto out_bad;
            }
            s->content = qemu_mallocz(s->content_len);
            s->state = VA_READ_BODY;
            TRACE("read http header:\n<<<%s>>>\n", s->hdr);
        }
    case VA_READ_BODY:
        while(s->content_pos < s->content_len) {
            ret = read(fd, s->content + s->content_pos,
                       s->content_len - s->content_pos);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK
                    || errno == EINTR) {
                    return;
                } else {
                    LOG("error reading connection: %s", strerror(errno));
                    goto out_bad;
                }
            } else if (ret == 0) {
                LOG("connection closed unexpectedly:"
                    " read %u bytes, expected %u bytes",
                    (unsigned int)s->content_pos, (unsigned int)s->content_len);
                goto out_reconnect;
            }
            s->content_pos += ret;
        }

        TRACE("read http content:\n<<<%s>>>\n", s->content);
        http_status = VA_HTTP_STATUS_OK;
        goto out;
    default:
        LOG("unknown state");
        goto out_bad;
    }

out_reconnect:
    /* we should only ever get a read = 0 if we're using virtio and the host
     * closed it's connection. this is a corner case that will cause spinning
     * until we close and reconnect, so handle this here.
     */
    /*
    if (strcmp(va_state->channel_method, "virtio-serial") == 0) {
        qemu_set_fd_handler(va_state->fd, NULL, NULL, NULL);
        close(va_state->fd);
        va_connect();
        qemu_set_fd_handler(va_state->fd, va_http_read_handler, NULL, NULL);
        return;
    }
    */
out_bad:
    http_status = VA_HTTP_STATUS_ERROR;
out:
    /* handle the response or request we just read */
    s->read_cb(http_status, s->content, s->content_len, s->hdr_client_tag,
               s->http_type);
    /* restart read handler */
    va_http_read_handler_reset();
    http_status = VA_HTTP_STATUS_NEW;
}

static void va_http_send_handler(void *opaque)
{
    VAHTState *s = &va_state->send_state;
    enum va_http_status http_status;
    int fd = va_state->fd;
    int ret;

    TRACE("called");

    switch (s->state) {
    case VA_SEND_START:
        s->state = VA_SEND_HDR;
    case VA_SEND_HDR:
        do {
            ret = write(fd, s->hdr + s->hdr_pos, s->hdr_len - s->hdr_pos);
            if (ret <= 0) {
                break;
            }
            s->hdr_pos += ret;
        } while (s->hdr_pos < s->hdr_len);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            } else {
                LOG("error writing header: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_bad;
        } else {
            s->state = VA_SEND_BODY;
            TRACE("sent http header:\n<<<%s>>>", s->hdr);
        }
    case VA_SEND_BODY:
        do {
            ret = write(fd, s->content + s->content_pos,
                        s->content_len - s->content_pos);
            if (ret <= 0) {
                break;
            }
            s->content_pos += ret;
        } while (s->content_pos < s->content_len);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            } else {
                LOG("error writing content: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_bad;
        } else {
            http_status = VA_HTTP_STATUS_OK;
            TRACE("set http content:\n<<<%s>>>", s->content);
            goto out;
        }
    default:
        LOG("unknown state");
        goto out_bad;
    }

out_bad:
    http_status = VA_HTTP_STATUS_ERROR;
out:
    s->send_cb(http_status, s->content, s->content_len);
    qemu_set_fd_handler(fd, va_http_read_handler, NULL, NULL);
}

/***********************************************************/
/* functions for starting/managing client/server rpc jobs */

static int va_send_server_response(VAServerJob *server_job)
{
    VAHTState http_state;
    TRACE("called");
    http_state.content = XMLRPC_MEMBLOCK_CONTENTS(char, server_job->resp_data);
    TRACE("sending response: %s", http_state.content);
    http_state.content_len = XMLRPC_MEMBLOCK_SIZE(char,
                                                  server_job->resp_data);
    http_state.content_pos = 0;
    http_state.hdr_pos = 0;
    pstrcpy(http_state.hdr_client_tag, 64, server_job->client_tag);
    http_state.state = VA_SEND_START;
    http_state.send_cb = va_server_send_cb;
    va_http_hdr_init(&http_state, VA_HTTP_TYPE_RESPONSE);
    va_state->send_state = http_state;
    qemu_set_fd_handler(va_state->fd, va_http_read_handler,
                        va_http_send_handler, NULL);
    return 0;
}

static int va_send_client_request(VAClientJob *client_job)
{
    VAHTState http_state;
    TRACE("called");
    http_state.content = XMLRPC_MEMBLOCK_CONTENTS(char, client_job->req_data);
    TRACE("sending request: %s", http_state.content);
    http_state.content_len = XMLRPC_MEMBLOCK_SIZE(char,
                                                  client_job->req_data);
    http_state.content_pos = 0;
    http_state.hdr_pos = 0;
    http_state.state = VA_SEND_START;
    http_state.send_cb = va_client_send_cb;
    pstrcpy(http_state.hdr_client_tag, 64, client_job->client_tag);
    va_http_hdr_init(&http_state, VA_HTTP_TYPE_REQUEST);
    va_state->send_state = http_state;
    qemu_set_fd_handler(va_state->fd, va_http_read_handler,
                        va_http_send_handler, NULL);
    return 0;
}

/* do some sanity checks before setting client state */
static bool va_set_client_state(enum va_client_state client_state)
{
    TRACE("setting client state to %d", client_state);
    switch (client_state) {
    case VA_CLIENT_IDLE:
        assert(va_state->client_state == VA_CLIENT_IDLE ||
               va_state->client_state == VA_CLIENT_WAIT);
        break;
    case VA_CLIENT_SEND:
        assert(va_state->client_state == VA_CLIENT_IDLE);
        break;
    case VA_CLIENT_WAIT:
        assert(va_state->client_state == VA_CLIENT_SEND);
        break;
    default:
        LOG("invalid client state");
        return false;
    }
    va_state->client_state = client_state;
    return true;
}

/* do some sanity checks before setting server state */
static bool va_set_server_state(enum va_server_state server_state)
{
    TRACE("setting server state to %d", server_state);
    switch (server_state) {
    case VA_SERVER_IDLE:
        assert(va_state->server_state == VA_SERVER_IDLE ||
               va_state->server_state == VA_SERVER_SEND);
        break;
    case VA_SERVER_WAIT:
        assert(va_state->server_state == VA_SERVER_IDLE);
        break;
    case VA_SERVER_SEND:
        assert(va_state->server_state == VA_SERVER_IDLE ||
               va_state->server_state == VA_SERVER_WAIT);
        break;
    default:
        LOG("invalid server state");
        return false;
    }
    va_state->server_state = server_state;
    return true;
}

/* xmit the next client/server job. for the client this entails sending
 * a request to the remote server. for the server this entails sending a
 * response to the remote client
 *
 * currently we only do one client job or one server job at a time. for
 * situations where we start a client job but recieve a server job (remote
 * rpc request) we go ahead and handle the server job before returning to
 * handling the client job. TODO: there is potential for pipelining
 * requests/responses for more efficient use of the channel.
 *
 * in all cases, we can only kick off client requests or server responses 
 * when the send side of the channel is not being used
 */
static int va_kick(void)
{
    VAServerJob *server_job;
    VAClientJob *client_job;
    int ret;

    TRACE("called");

    /* handle server jobs first */
    if (QTAILQ_EMPTY(&va_state->server_jobs)) {
        assert(va_set_server_state(VA_SERVER_IDLE));
    } else {
        TRACE("handling server job queue");
        if (va_state->client_state == VA_CLIENT_SEND) {
            TRACE("send channel busy, deferring till available");
            assert(va_set_server_state(VA_SERVER_WAIT));
            goto out;
        }
        if (va_state->server_state == VA_SERVER_SEND) {
            TRACE("current server job already sending");
            goto out;
        }
        TRACE("send server response");
        server_job = QTAILQ_FIRST(&va_state->server_jobs);

        /* set up the send handler for the response */
        ret = va_send_server_response(server_job);
        if (ret != 0) {
            LOG("error setting up send handler for server response");
            goto out_bad;
        }
        assert(va_set_server_state(VA_SERVER_SEND));
        goto out;
    }

    /* handle client jobs if nothing to do for server */
    if (QTAILQ_EMPTY(&va_state->client_jobs)) {
        assert(va_set_client_state(VA_CLIENT_IDLE));
    } else {
        TRACE("handling client job queue");
        if (va_state->client_state != VA_CLIENT_IDLE) {
            TRACE("client job in progress, returning");
            goto out;
        }

        TRACE("sending new client request");
        client_job = QTAILQ_FIRST(&va_state->client_jobs);
        /* set up the send handler for the request, then put it on the
         * wait queue till response is read
         */
        ret = va_send_client_request(client_job);
        if (ret != 0) {
            LOG("error setting up sendhandler for client request");
            goto out_bad;
        }
        assert(va_set_client_state(VA_CLIENT_SEND));
    }

out:
    return 0;
out_bad:
    return ret;
}

/* push new client job onto queue, */
static int va_push_client_job(VAClientJob *client_job)
{
    TRACE("called");
    assert(client_job != NULL);
    if (va_state->client_job_count >= VA_CLIENT_JOBS_MAX) {
        LOG("client job queue limit exceeded");
        return -ENOBUFS;
    }
    QTAILQ_INSERT_TAIL(&va_state->client_jobs, client_job, next);
    va_state->client_job_count++;

    return va_kick();
}

/* pop client job off queue. this should only be done when we're done with
 * both sending the request and recieving the response
 */
static VAClientJob *va_pop_client_job(void)
{
    VAClientJob *client_job = va_current_client_job();
    TRACE("called");
    if (client_job != NULL) {
        QTAILQ_REMOVE(&va_state->client_jobs, client_job, next);
        va_state->client_job_count--;
        assert(va_set_client_state(VA_CLIENT_IDLE));
    }
    return client_job;
}

/* push new server job onto the queue */
static int va_push_server_job(VAServerJob *server_job)
{
    TRACE("called");
    if (va_state->server_job_count >= VA_SERVER_JOBS_MAX) {
        LOG("server job queue limit exceeded");
        return -ENOBUFS;
    }
    QTAILQ_INSERT_TAIL(&va_state->server_jobs, server_job, next);
    va_state->server_job_count++;
    return va_kick();
}

/* pop server job off queue. this should only be done when we're ready to
 * send the rpc response back to the remote client
 */
static VAServerJob *va_pop_server_job(void) {
    VAServerJob *server_job = QTAILQ_FIRST(&va_state->server_jobs);
    TRACE("called");
    if (server_job != NULL) {
        QTAILQ_REMOVE(&va_state->server_jobs, server_job, next);
        va_state->server_job_count--;
        assert(va_set_server_state(VA_SERVER_IDLE));
    }

    return server_job;
}

static VAClientJob *va_client_job_new(xmlrpc_mem_block *req_data,
                                      VAClientCallback *cb,
                                      MonitorCompletion *mon_cb,
                                      void *mon_data)
{
    VAClientJob *cj = qemu_mallocz(sizeof(VAClientJob));
    TRACE("called");
    cj->req_data = req_data;
    cj->cb = cb;
    cj->mon_cb = mon_cb;
    cj->mon_data = mon_data;
    /* TODO: use uuid's, or something akin */
    strcpy(cj->client_tag, "testtag");

    return cj;
}

static VAServerJob *va_server_job_new(xmlrpc_mem_block *resp_data,
                                      const char client_tag[64])
{
    VAServerJob *sj = qemu_mallocz(sizeof(VAServerJob));
    TRACE("called");
    sj->resp_data = resp_data;
    pstrcpy(sj->client_tag, 64, client_tag);

    return sj;
}

/* create new client job and then put it on the queue. this can be
 * called externally from virtagent. Since there can only be one virtagent
 * instance we access state via an object-scoped global rather than pass
 * it around.
 *
 * if this is successful virtagent will handle cleanup of req_xml after
 * making the appropriate callbacks, otherwise callee should handle it
 */
int va_client_job_add(xmlrpc_mem_block *req_xml, VAClientCallback *cb,
                      MonitorCompletion *mon_cb, void *mon_data)
{
    int ret;
    VAClientJob *client_job;
    TRACE("called");

    client_job = va_client_job_new(req_xml, cb, mon_cb, mon_data);
    if (client_job == NULL) {
        return -EINVAL;
    }

    ret = va_push_client_job(client_job);
    if (ret != 0) {
        LOG("error adding client to queue: %s", strerror(ret));
        qemu_free(client_job);
        return ret;
    }

    return 0;
}

/* create new server job and then put it on the queue in wait state */
int va_server_job_add(xmlrpc_mem_block *resp_xml, const char client_tag[64])
{
    VAServerJob *server_job;
    TRACE("called");

    server_job = va_server_job_new(resp_xml, client_tag);
    assert(server_job != NULL);
    va_push_server_job(server_job);
    return 0;
}

static int va_connect(void)
{
    QemuOpts *opts;
    int fd, ret = 0;

    TRACE("called");
    if (va_state->channel_method == NULL) {
        LOG("no channel method specified");
        return -EINVAL;
    }
    if (va_state->channel_path == NULL) {
        LOG("no channel path specified");
        return -EINVAL;
    }

    if (strcmp(va_state->channel_method, "unix-connect") == 0) {
        TRACE("connecting to %s", va_state->channel_path);
        opts = qemu_opts_create(qemu_find_opts("chardev"), NULL, 0);
        qemu_opt_set(opts, "path", va_state->channel_path);
        fd = unix_connect_opts(opts);
        if (fd == -1) {
            qemu_opts_del(opts);
            LOG("error opening channel: %s", strerror(errno));
            return -errno;
        }
        qemu_opts_del(opts);
        socket_set_nonblock(fd);
    } else if (strcmp(va_state->channel_method, "virtio-serial") == 0) {
        if (va_state->is_host) {
            LOG("specified channel method not available for host");
            return -EINVAL;
        }
        if (va_state->channel_path == NULL) {
            va_state->channel_path = VA_GUEST_PATH_VIRTIO_DEFAULT;
        }
        TRACE("opening %s", va_state->channel_path);
        fd = qemu_open(va_state->channel_path, O_RDWR);
        if (fd == -1) {
            LOG("error opening channel: %s", strerror(errno));
            return -errno;
        }
        ret = fcntl(fd, F_GETFL);
        if (ret < 0) {
            LOG("error getting channel flags: %s", strerror(errno));
            return -errno;
        }
        ret = fcntl(fd, F_SETFL, ret | O_ASYNC);
        if (ret < 0) {
            LOG("error setting channel flags: %s", strerror(errno));
            return -errno;
        }
    } else {
        LOG("invalid channel method");
        return -EINVAL;
    }

    va_state->fd = fd;
    return 0;
}

int va_init(VAContext ctx)
{
    VAState *s;
    int ret;

    TRACE("called");
    if (va_state) {
        LOG("virtagent already initialized");
        return -EPERM;
    }

    s = qemu_mallocz(sizeof(VAState));

    ret = va_server_init(&s->server_data, ctx.is_host);
    if (ret) {
        LOG("error initializing virtagent server");
        goto out_bad;
    }
    ret = va_client_init(&s->client_data);
    if (ret) {
        LOG("error initializing virtagent client");
        goto out_bad;
    }

    s->client_state = VA_CLIENT_IDLE;
    s->server_state = VA_SERVER_IDLE;
    QTAILQ_INIT(&s->client_jobs);
    QTAILQ_INIT(&s->server_jobs);
    s->read_state.state = VA_READ_START;
    s->read_state.read_cb = va_http_read_cb;
    s->channel_method = ctx.channel_method;
    s->channel_path = ctx.channel_path;
    s->is_host = ctx.is_host;
    va_state = s;
    
    /* connect to our end of the channel */
    ret = va_connect();
    if (ret) {
        LOG("error connecting to channel");
        goto out_bad;
    }

    /* start listening for requests/responses */
    qemu_set_fd_handler(va_state->fd, va_http_read_handler, NULL, NULL);

    return 0;
out_bad:
    qemu_free(s);
    return ret;
}
