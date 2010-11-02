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

#define VA_READ true
#define VA_SEND false

enum va_rpc_type {
    VA_RPC_REQUEST,
    VA_RPC_RESPONSE,
};

typedef struct VARPCState {
    char hdr[VA_HDR_LEN_MAX];
    int fd;
    size_t hdr_len;
    size_t hdr_pos;
    enum {
        VA_READ_START,
        VA_READ_HDR,
        VA_READ_BODY,
        VA_SEND_START,
        VA_SEND_HDR,
        VA_SEND_BODY,
    } state;
    enum va_rpc_type rpc_type;
    char *content;
    size_t content_len;
    size_t content_pos;
    VARPCData *data;
} VARPCState;

static void va_rpc_read_handler(void *opaque);
static void va_rpc_send_handler(void *opaque);

static int end_of_header(char *buf, int end_pos)
{
    return !strncmp(buf+(end_pos-2), "\n\r\n", 3);
}

static void va_rpc_hdr_init(VARPCState *s) {
    const char *preamble;

    TRACE("called");
    /* essentially ignored in the context of virtagent, but might as well */
    if (s->rpc_type == VA_RPC_REQUEST) {
        preamble = "POST /RPC2 HTTP/1.1";
    } else if (s->rpc_type == VA_RPC_RESPONSE) {
        preamble = "HTTP/1.1 200 OK";
    } else {
        s->hdr_len = 0;
        return;
    }

    s->hdr_len = sprintf(s->hdr,
                         "%s" EOL
                         "Content-Type: text/xml" EOL
                         "Content-Length: %u" EOL EOL,
                         preamble,
                         (uint32_t)s->content_len);
}

static void va_rpc_parse_hdr(VARPCState *s)
{
    int i, line_pos = 0;
    char line_buf[4096];

    for (i = 0; i < VA_HDR_LEN_MAX; ++i) {
        if (s->hdr[i] != '\n') {
            /* read line */
            line_buf[line_pos++] = s->hdr[i];
        } else {
            /* process line */
            /* TODO: don't use strncasecmp, "Content-Length" should be good */
            if (strncasecmp(line_buf, "content-length: ", 16) == 0) {
                s->content_len = atoi(&line_buf[16]);
                return;
            }
            line_pos = 0;
        }
    }
}

static VARPCState *va_rpc_state_new(VARPCData *data, int fd,
                                    enum va_rpc_type rpc_type, bool read)
{
    VARPCState *s = qemu_mallocz(sizeof(VARPCState));

    s->rpc_type = rpc_type;
    s->fd = fd;
    s->data = data;
    if (s->data == NULL) {
        goto EXIT_BAD;
    }

    if (read) {
        s->state = VA_READ_START;
        s->content = NULL;
    } else {
        s->state = VA_SEND_START;
        if (rpc_type == VA_RPC_REQUEST) {
            s->content = XMLRPC_MEMBLOCK_CONTENTS(char, s->data->send_req_xml);
            s->content_len = XMLRPC_MEMBLOCK_SIZE(char, s->data->send_req_xml);
        } else if (rpc_type == VA_RPC_RESPONSE) {
            s->content = XMLRPC_MEMBLOCK_CONTENTS(char, s->data->send_resp_xml);
            s->content_len = XMLRPC_MEMBLOCK_SIZE(char, s->data->send_resp_xml);
        } else {
            LOG("unknown rcp type");
            goto EXIT_BAD;
        }
        va_rpc_hdr_init(s);
        if (s->hdr_len == 0) {
            LOG("failed to initialize http header");
            goto EXIT_BAD;
        }
    }

    return s;
EXIT_BAD:
    qemu_free(s);
    return NULL;
}

/* called by va_rpc_read_handler after reading requests */
static int va_rpc_send_response(VARPCData *data, int fd)
{
    VARPCState *s = va_rpc_state_new(data, fd, VA_RPC_RESPONSE, VA_SEND);

    TRACE("called");
    if (s == NULL) {
        LOG("failed to set up RPC state");
        return -1;
    }
    TRACE("setting up send handler for RPC request");
    vp_set_fd_handler(fd, NULL, va_rpc_send_handler, s);

    return 0;
}

static void va_rpc_read_handler_completion(VARPCState *s) {
    int ret;

    if (s->rpc_type == VA_RPC_REQUEST) {
        /* server read request, call it's cb function then set up
         * a send handler for the rpc response if there weren't any
         * communication errors
         */ 
        s->data->cb(s->data);
        if (s->data->status == VA_RPC_STATUS_OK) {
            ret = va_rpc_send_response(s->data, s->fd);
            if (ret != 0) {
                LOG("error setting up send handler for rpc response");
            }
        } else {
            LOG("error reading rpc request, skipping response");
            vp_set_fd_handler(s->fd, NULL, NULL, NULL);
            closesocket(s->fd);
            qemu_free(s->data);
        }
    } else if (s->rpc_type == VA_RPC_RESPONSE) {
        /* client read response, call it's cb function and complete
         * the RPC
         */
        s->data->cb(s->data);
        vp_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        qemu_free(s->data);
    } else {
        LOG("unknown rpc_type");
    }
    if (s->content != NULL) {
        qemu_free(s->content);
    }
    qemu_free(s);
}

static void va_rpc_read_handler(void *opaque)
{
    VARPCState *s = opaque;
    int ret;

    TRACE("called with opaque: %p", opaque);

    switch (s->state) {
    case VA_READ_START:
        s->state = VA_READ_HDR;
    case VA_READ_HDR:
        while((ret = read(s->fd, s->hdr + s->hdr_pos, 1)) > 0
              && s->hdr_pos < VA_HDR_LEN_MAX) {
            s->hdr_pos += ret;
            if (end_of_header(s->hdr, s->hdr_pos - 1)) {
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
            LOG("connected closed unexpectedly");
            goto out_bad;
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
        }
    case VA_READ_BODY:
        while(s->content_pos < s->content_len) {
            ret = read(s->fd, s->content + s->content_pos,
                       MIN(s->content_len - s->content_pos, 4096));
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                } else {
                    LOG("error reading connection: %s", strerror(errno));
                    goto out_bad;
                }
            } else if (ret == 0) {
                LOG("connection closed unexpectedly:"
                    " read %u bytes, expected %u bytes",
                    (unsigned int)s->content_pos, (unsigned int)s->content_len);
                goto out_bad;
            }
            s->content_pos += ret;
        }

        if (s->rpc_type == VA_RPC_REQUEST) {
            s->data->req_xml = s->content;
            s->data->req_xml_len = s->content_len;
        } else if (s->rpc_type == VA_RPC_RESPONSE) {
            s->data->resp_xml = s->content;
            s->data->resp_xml_len = s->content_len;
        }
        s->data->status = VA_RPC_STATUS_OK;
        goto out;
    default:
        LOG("unknown state");
        goto out_bad;
    }

out_bad:
    s->data->status = VA_RPC_STATUS_ERR;
out:
    va_rpc_read_handler_completion(s);
}

/* called by va_rpc_send_handler after sending requests */
static int va_rpc_read_response(VARPCData *data, int fd)
{
    VARPCState *s = va_rpc_state_new(data, fd, VA_RPC_RESPONSE, VA_READ);

    TRACE("called");
    if (s == NULL) {
        LOG("failed to set up RPC state");
        return -1;
    }
    TRACE("setting up send handler for RPC request");
    vp_set_fd_handler(fd, NULL, va_rpc_read_handler, s);

    return 0;
}

static void va_rpc_send_handler_completion(VARPCState *s) {
    int ret;

    if (s->rpc_type == VA_RPC_REQUEST) {
        /* client sent request. free request's memblock, and set up read
         * handler for server response if there weren't any communication
         * errors
         */
        XMLRPC_MEMBLOCK_FREE(char, s->data->send_req_xml);
        if (s->data->status == VA_RPC_STATUS_OK) {
            ret = va_rpc_read_response(s->data, s->fd);
            if (ret != 0) {
                LOG("error setting up read handler for rpc response");
            }
        } else {
            s->data->cb(s->data);
            LOG("error sending rpc request, skipping response");
            vp_set_fd_handler(s->fd, NULL, NULL, NULL);
            closesocket(s->fd);
            qemu_free(s->data);
        }
    } else if (s->rpc_type == VA_RPC_RESPONSE) {
        /* server sent response. call it's cb once more, then free
         * response's memblock and complete the RPC
         */
        s->data->cb(s->data);
        XMLRPC_MEMBLOCK_FREE(char, s->data->send_resp_xml);
        vp_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        qemu_free(s->data);
    } else {
        LOG("unknown rpc_type");
    }
    qemu_free(s);
}

static void va_rpc_send_handler(void *opaque)
{
    VARPCState *s = opaque;
    int ret;

    TRACE("called with opaque: %p", opaque);

    switch (s->state) {
    case VA_SEND_START:
        s->state = VA_SEND_HDR;
    case VA_SEND_HDR:
        do {
            ret = write(s->fd, s->hdr + s->hdr_pos,
                        MIN(s->hdr_len - s->hdr_pos, 4096));
            if (ret <= 0) {
                break;
            }
            s->hdr_pos += ret;
        } while (s->hdr_pos < s->hdr_len);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            } else {
                LOG("error reading connection: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_bad;
        } else {
            s->state = VA_SEND_BODY;
        }
    case VA_SEND_BODY:
        do {
            ret = write(s->fd, s->content + s->content_pos,
                        MIN(s->content_len - s->content_pos, 4096));
            if (ret <= 0) {
                break;
            }
            s->content_pos += ret;
        } while (s->content_pos < s->content_len);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            } else {
                LOG("error reading connection: %s", strerror(errno));
                goto out_bad;
            }
        } else if (ret == 0) {
            LOG("connected closed unexpectedly");
            goto out_bad;
        } else {
            s->data->status = VA_RPC_STATUS_OK;
            goto out;
        }
    default:
        LOG("unknown state");
        goto out_bad;
    }

out_bad:
    s->data->status = VA_RPC_STATUS_ERR;
out:
    va_rpc_send_handler_completion(s);
}

/* called by rpc client
 * one callback to data->cb after response is read.
 * data and data->send_req_xml should be allocated by caller,
 * callee will de-allocate these after calling data->cb(data)
 *
 * if non-zero returned however, caller should free data and hanging refs
 */ 
int va_rpc_send_request(VARPCData *data, int fd)
{
    VARPCState *s = va_rpc_state_new(data, fd, VA_RPC_REQUEST, VA_SEND);

    TRACE("called");
    if (s == NULL) {
        LOG("failed to set up RPC state");
        return -1;
    }
    TRACE("setting up send handler for RPC request");
    vp_set_fd_handler(fd, NULL, va_rpc_send_handler, s);

    return 0;
}

/* called by rpc server
 * one callback to current data->cb after read, one callback after send.
 * data should be allocated by caller, data->send_resp_xml should be
 * allocated by first data->cb(data) callback, "callee" will de-allocate
 * data and data->send_resp_xml after sending rpc response
 *
 * if non-zero returned however, caller should free data and hanging refs
 */
int va_rpc_read_request(VARPCData *data, int fd)
{
    VARPCState *s = va_rpc_state_new(data, fd, VA_RPC_REQUEST, VA_READ);

    TRACE("called");
    if (s == NULL) {
        LOG("failed to set up RPC state");
        return -1;
    }
    TRACE("setting up read handler for RPC request");
    vp_set_fd_handler(fd, va_rpc_read_handler, NULL, s);
    return 0;
}
