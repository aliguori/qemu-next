/*
 * virt-agent - common host/guest RPC functions
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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>
#include <xmlrpc-c/server.h>
#include "virtagent-common.h"

static int write_all(int fd, const void *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = write(fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                LOG("write() failed");
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

static int read_all(int fd, void *buf, int len)
{
    int ret, remaining;

    remaining = len;
    while (remaining > 0) {
        ret = read(fd, buf, remaining);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                LOG("read failed");
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            remaining -= ret;
        }
    }
    return len - remaining;
}

static int read_line(int fd, void *buf, int len)
{
    int ret, remaining;
    remaining = len;
    while (remaining > 0) {
        ret = read(fd, buf, 1);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                LOG("read failed");
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            remaining--;
            buf++;
            if (*((char *)buf-1) == '\n')
                break;
        }
    }
    memset(buf, 0, remaining);
    return len - remaining;
}

typedef struct va_http {
    char *preamble;
    int content_length;
    int content_read;
    char *content;
} va_http;

typedef struct HttpReadState {
    int fd;
    char hdr_buf[4096];
    int hdr_pos;
    va_http http;
    enum {
        IN_RESP_HEADER = 0,
        IN_RESP_BODY,
    } state;
    RPCRequest *rpc_data;
} HttpReadState;

static int end_of_header(char *buf, int end_pos) {
    return !strncmp(buf+(end_pos-2), "\n\r\n", 3);
}

static void parse_hdr(va_http *http, char *buf, int len) {
    int i, line_pos=0;
    char line_buf[4096];

    for (i=0; i<len; ++i) {
        if (buf[i] != '\n') {
            /* read line */
            line_buf[line_pos++] = buf[i];
        } else {
            /* process line */
            if (strncasecmp(line_buf, "content-length: ", 16) == 0) {
                http->content_length = atoi(&line_buf[16]);
                return;
            }
            line_pos = 0;
        }
    }
}

static void http_read_handler(void *opaque) {
    HttpReadState *s = opaque;
    int ret;

    TRACE("called with opaque: %p", opaque);
    if (s->state == IN_RESP_HEADER) {
        while((ret = read(s->fd, s->hdr_buf + s->hdr_pos, 1)) > 0) {
            //TRACE("buf: %c", s->hdr_buf[0]);
            s->hdr_pos += ret;
            if (end_of_header(s->hdr_buf, s->hdr_pos - 1)) {
                s->state = IN_RESP_BODY;
                break;
            }
        }
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            } else {
                LOG("error reading connection: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connection closed unexpectedly");
            goto out_bad;
        } else {
            s->http.content_length = -1;
            parse_hdr(&s->http, s->hdr_buf, s->hdr_pos);
            if (s->http.content_length == -1) {
                LOG("malformed http header");
                goto out_bad;
            }
            s->http.content = qemu_mallocz(s->http.content_length);
            goto do_resp_body;
        }
    } else if (s->state == IN_RESP_BODY) {
do_resp_body:
        while(s->http.content_read < s->http.content_length) {
            ret = read(s->fd, s->http.content + s->http.content_read, 4096);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                } else {
                    LOG("error reading connection: %s", strerror(errno));
                    qemu_free(s->http.content);
                    goto out;
                }
            } else if (ret == 0) {
                LOG("connection closed unexpectedly, expected %d more bytes",
                    s->http.content_length - s->http.content_read);
                goto out_bad;
            }
            s->http.content_read += ret;
        }
        s->rpc_data->resp_xml = s->http.content;
        s->rpc_data->resp_xml_len = s->http.content_length;
        goto out;
    }
out_bad:
    s->rpc_data->resp_xml = NULL;
out:
    sleep(4);
    vp_set_fd_handler(s->fd, NULL, NULL, NULL);
    s->rpc_data->cb(s->rpc_data);
    qemu_free(s);
}

static int write_hdr(int fd, const va_http *http, bool request)
{
    int hdr_len;
    char *hdr;
    const char *preamble;

    /* essentially ignored in the context of virtagent, but might as well */
    preamble = request ? "POST /RPC2 HTTP/1.1" : "HTTP/1.1 200 OK";

    hdr_len = asprintf(&hdr,
                      "%s" EOL
                      "Content-Type: text/xml" EOL
                      "Content-Length: %u" EOL EOL,
                      preamble,
                      http->content_length);
    write_all(fd, hdr, hdr_len);

    return 0;
}

static int read_hdr(int fd, va_http *http)
{
    bool first = true;
    char line[4096];
    int ret;

    http->preamble = NULL;
    http->content_length = -1;

    do {
        ret = read_line(fd, &line, sizeof(line));
        if (ret <= 0) {
            LOG("error reading from connection");
            ret = -EPIPE;
            return ret;
        }
        TRACE("read line: %s", line);
        if (first) {
            http->preamble = line;
            first = false;
        }
        if (strncasecmp(line, "content-length: ", 16) == 0) {
            TRACE("hi");
            http->content_length = atoi(&line[16]);
        }
    } while (strcmp(line, "\r\n") && strcmp(line, "\n"));

    if (http->preamble == NULL) {
        LOG("header missing preamble");
        return -1;
    } else if (http->content_length == -1) {
        LOG("missing content length");
        return -1;
    }

    return 0;
}

static int send_http(int fd, const va_http *http, bool request)
{
    int ret;

    ret = write_hdr(fd, http, request);
    if (ret != 0) {
        LOG("error sending header");
        return -1;
    }

    ret = write_all(fd, http->content, http->content_length);
    if (ret != http->content_length) {
        LOG("error sending content");
        return -1;
    }

    return 0;
}

static int send_http_request(int fd, const va_http *http)
{
    return send_http(fd, http, true);
}

static int send_http_response(int fd, const va_http *http)
{
    return send_http(fd, http, false);
}

static int get_http(int fd, va_http *http)
{
    int ret;

    ret = read_hdr(fd, http);
    if (ret != 0) {
        LOG("error reading header");
        return -1;
    }

    http->content = qemu_malloc(http->content_length);
    ret = read_all(fd, http->content, http->content_length);
    if (ret != http->content_length) {
        LOG("error reading content");
        return -1;
    }

    TRACE("http:\n%s", http->content);

    return 0;
}

/* send http-encoded xmlrpc response */
int va_send_rpc_response(int fd, const xmlrpc_mem_block *resp_xml)
{
    int ret;
    va_http http_resp;

    http_resp.content = XMLRPC_MEMBLOCK_CONTENTS(char, resp_xml);
    http_resp.content_length = XMLRPC_MEMBLOCK_SIZE(char, resp_xml);

    TRACE("sending rpc response");
    ret = send_http_response(fd, &http_resp);
    if (ret != 0) {
        LOG("failed to send rpc response");
        return -1;
    }

    return 0;
}

/* read xmlrpc payload from http request */
int va_get_rpc_request(int fd, char **req_xml, int *req_len)
{
    int ret;
    va_http http_req;

    ret = get_http(fd, &http_req);
    if (ret != 0) {
        LOG("failed to get RPC request");
        return -1;
    }

    *req_xml = http_req.content;
    *req_len = http_req.content_length;

    return 0;
}

/* send an RPC request and retrieve the response */
int va_transport_rpc_call(int fd, RPCRequest *rpc_data)
{
    int ret;
    struct va_http http_req;
    HttpReadState *read_state;

    http_req.content = XMLRPC_MEMBLOCK_CONTENTS(char, rpc_data->req_xml);
    http_req.content_length = XMLRPC_MEMBLOCK_SIZE(char, rpc_data->req_xml);

    /* TODO: this should be done asynchronously */
    TRACE("sending rpc request");
    ret = send_http_request(fd, &http_req);
    if (ret != 0) {
        LOG("failed to send rpc request");
        return -1;
    }

    TRACE("setting up rpc response handler");
    read_state = qemu_mallocz(sizeof(HttpReadState));
    read_state->fd = fd;
    read_state->rpc_data = rpc_data;
    read_state->state = IN_RESP_HEADER;
    vp_set_fd_handler(fd, http_read_handler, NULL, read_state);

    return 0;
}
