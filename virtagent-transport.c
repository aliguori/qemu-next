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

typedef struct VAHTState {
    enum {
        VA_SEND_START = 0,
        VA_SEND_HDR,
        VA_SEND_BODY,
        VA_SEND_COMPLETE,
        VA_READ_START,
        VA_READ_HDR,
        VA_READ_BODY,
        VA_READ_COMPLETE,
    } state;
    char hdr[VA_HDR_LEN_MAX];
    char hdr_client_tag[64];
    size_t hdr_len;
    size_t hdr_pos;
    char *content;
    size_t content_len;
    size_t content_pos;
    const void *opaque;
    VAHTSendCallback *send_cb;
    enum va_http_type http_type;
} VAHTState;

extern VAState *va_state;
VAHTState va_send_state = {
    .state = VA_SEND_START,
};
VAHTState va_read_state = {
    .state = VA_READ_START,
};

/* utility functions for handling http calls */

static void va_http_hdr_init(VAHTState *s, enum va_http_type http_type)
{
    const char *preamble;

    TRACE("called");
    /* essentially ignored in the context of virtagent, but might as well */
    if (http_type == VA_HTTP_TYPE_REQUEST) {
        preamble = "POST /RPC2 HTTP/1.1";
    } else if (http_type == VA_HTTP_TYPE_RESPONSE) {
        preamble = "HTTP/1.1 200 OK";
    } else {
        LOG("unknown http type");
        s->hdr_len = 0;
        return;
    }
    memset(s->hdr, 0, VA_HDR_LEN_MAX);
    s->hdr_len = sprintf(s->hdr,
                         "%c%s" EOL
                         "Content-Type: text/xml" EOL
                         "Content-Length: %u" EOL
                         "X-Virtagent-Client-Tag: %s" EOL EOL,
                         VA_SENTINEL,
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

static int va_end_of_header(char *buf, int end_pos)
{
    return !strncmp(buf+(end_pos-2), "\n\r\n", 3);
}

static void va_http_read_handler_reset(void)
{
    VAHTState *s = &va_read_state;
    TRACE("called");
    s->state = VA_READ_START;
    s->http_type = VA_HTTP_TYPE_UNKNOWN;
    s->hdr_pos = 0;
    s->content_len = 0;
    s->content_pos = 0;
    memset(s->hdr_client_tag, 0, 64);
    strcpy(s->hdr_client_tag, "none");
    s->content = NULL;
}

static void va_http_process(char *content, size_t content_len,
                            const char *tag, enum va_http_type type)
{
    TRACE("marker");
    if (type == VA_HTTP_TYPE_REQUEST) {
        va_server_job_create(content, content_len, tag);
    } else if (type == VA_HTTP_TYPE_RESPONSE) {
        va_client_read_response_done(content, content_len, tag);
    } else {
        LOG("unknown http type");
    }
}

/* read/send handlers */

void va_http_read_handler(void *opaque)
{
    VAHTState *s = &va_read_state;
    enum va_http_status http_status;
    int fd = va_state->fd;
    int ret;
    uint8_t tmp;
    static int bytes_skipped = 0;

    TRACE("called with opaque: %p", opaque);

    switch (s->state) {
    case VA_READ_START:
        /* we may have gotten here due to a http error, indicating
         * a potential unclean state where we are not 'aligned' on http
         * boundaries. we should read till we hit the next http preamble
         * rather than assume we're at the start of an http header. since
         * we control the transport layer on both sides, we'll use a
         * more reliable sentinal character to mark/detect the start of
         * the header
         */
        while((ret = read(fd, &tmp, 1) > 0) > 0) {
            if (tmp == VA_SENTINEL) {
                break;
            }
            bytes_skipped += ret;
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
            goto out_bad_wait;
        } else {
            TRACE("found header, number of bytes skipped: %d",
                  bytes_skipped);
            bytes_skipped = 0;
            s->state = VA_READ_HDR;
        }
    case VA_READ_HDR:
        while((ret = read(fd, s->hdr + s->hdr_pos, 1)) > 0
              && s->hdr_pos < VA_HDR_LEN_MAX) {
            if (s->hdr[s->hdr_pos] == (char)VA_SENTINEL) {
                /* truncated header, toss it out and start over */
                LOG("truncated header detected");
                s->hdr_pos = 0;
            } else {
                s->hdr_pos += ret;
                if (va_end_of_header(s->hdr, s->hdr_pos - 1)) {
                    break;
                }
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
            goto out_bad_wait;
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
                goto out_bad_wait;
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

out_bad_wait:
    /* We should only ever get a ret = 0 if we're a guest and the host is
     * not connected. this would cause a guest to spin, and we can't do
     * any work in the meantime, so sleep for a bit here. We also know
     * we may go ahead and cancel any outstanding jobs at this point, though
     * it should be noted that we're still ultimately reliant on per-job
     * timeouts since we might not read EOF before host reconnect.
     */
    if (!va_state->is_host) {
        usleep(100 * 1000);
    }
out_bad:
    http_status = VA_HTTP_STATUS_ERROR;
out:
    s->state = VA_READ_COMPLETE;
    /* handle the response or request we just read */
    if (http_status == VA_HTTP_STATUS_OK) {
        va_http_process(s->content, s->content_len, s->hdr_client_tag, s->http_type);
    } else {
        LOG("http read error");
    }
    /* restart read handler */
    va_http_read_handler_reset();
    http_status = VA_HTTP_STATUS_NEW;
}

static void va_http_send_handler(void *opaque)
{
    VAHTState *s = &va_send_state;
    enum va_http_status http_status;
    int fd = va_state->fd;
    int ret;

    TRACE("called");

    switch (s->state) {
    case VA_SEND_START:
        s->state = VA_SEND_HDR;
        TRACE("preparing to send http header:\n<<<%s>>>", s->hdr);
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
            TRACE("preparing to send http content:\n<<<%s>>>", s->content);
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
            TRACE("sent http content:\n<<<%s>>>", s->content);
            sprintf(s->content, "THIS IS A TEST!!!!!!!!!!!!!");
            goto out;
        }
    default:
        LOG("unknown state");
        goto out_bad;
    }

out_bad:
    http_status = VA_HTTP_STATUS_ERROR;
out:
    s->state = VA_SEND_COMPLETE;
    qemu_set_fd_handler(fd, va_http_read_handler, NULL, NULL);
    s->send_cb(s->opaque);
}

static void va_send_handler_reset(void)
{
    TRACE("called");
    assert(va_send_state.state == VA_SEND_START ||
           va_send_state.state == VA_SEND_COMPLETE);
    va_send_state.content = NULL;
    va_send_state.content_len = 0;
    va_send_state.content_pos = 0;
    va_send_state.hdr_pos = 0;
    va_send_state.state = VA_SEND_START;
    memset(va_send_state.hdr_client_tag, 0, 64);
}

int va_xport_send_response(void *content, size_t content_len, const char *tag,
                           const void *opaque, VAHTSendCallback cb)
{
    TRACE("called");
    va_send_handler_reset();
    va_send_state.content = content;
    TRACE("sending response: %s", va_send_state.content);
    va_send_state.content_len = content_len;
    va_send_state.opaque = opaque;
    va_send_state.send_cb = cb;
    pstrcpy(va_send_state.hdr_client_tag, 63, tag);
    va_http_hdr_init(&va_send_state, VA_HTTP_TYPE_RESPONSE);
    qemu_set_fd_handler(va_state->fd, va_http_read_handler,
                        va_http_send_handler, NULL);
    return 0;
}

int va_xport_send_request(void *content, size_t content_len, const char *tag,
                          const void *opaque, VAHTSendCallback cb)
{
    TRACE("called");
    va_send_handler_reset();
    va_send_state.content = content;
    TRACE("sending request: %s", va_send_state.content);
    va_send_state.content_len = content_len;
    va_send_state.opaque = opaque;
    va_send_state.send_cb = cb;
    pstrcpy(va_send_state.hdr_client_tag, 63, tag);
    va_http_hdr_init(&va_send_state, VA_HTTP_TYPE_REQUEST);
    qemu_set_fd_handler(va_state->fd, va_http_read_handler,
                        va_http_send_handler, NULL);
    return 0;
}
