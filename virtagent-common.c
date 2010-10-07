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
    char *content;
} va_http;

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
int va_transport_rpc_call(int fd, xmlrpc_env *const env,
                          xmlrpc_mem_block *const req_xml,
                          xmlrpc_mem_block **resp_xml)
{
    int ret;
    char *resp_xml_buf;
    struct va_http http_req, http_resp;

    http_req.content = XMLRPC_MEMBLOCK_CONTENTS(char, req_xml);
    http_req.content_length = XMLRPC_MEMBLOCK_SIZE(char, req_xml);

    TRACE("sending rpc request");
    ret = send_http_request(fd, &http_req);
    if (ret != 0) {
        LOG("failed to send rpc request");
        return -1;
    }

    TRACE("getting rpc response");
    ret = get_http(fd, &http_resp);
    if (ret != 0) {
        LOG("failed to get rpc response");
        return -1;
    }

    *resp_xml = XMLRPC_MEMBLOCK_NEW(char, env, http_resp.content_length);
    resp_xml_buf = XMLRPC_MEMBLOCK_CONTENTS(char, *resp_xml);
    /* TODO: can we just *resp_xml = http_resp.content? what do these macros
     * really do?
     */
    memcpy(resp_xml_buf, http_resp.content, http_resp.content_length);
    qemu_free(http_resp.content);
    TRACE("read response xml:\n%s\n", resp_xml_buf);

    return 0;
}
