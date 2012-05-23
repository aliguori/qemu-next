/*
 *  passthrough TPM driver
 *
 *  Copyright (c) 2010, 2011 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 *  Copyright (C) 2011 IAIK, Graz University of Technology
 *    Author: Andreas Niederl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu-common.h"
#include "qemu-error.h"
#include "qemu_socket.h"
#include "tpm.h"
#include "hw/hw.h"
#include "hw/tpm_tis.h"
#include "hw/tpm_backend.h"
#include "hw/pc.h"

/* #define DEBUG_TPM */

#ifdef DEBUG_TPM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

/* data structures */

typedef struct TPMPassthruThreadParams {
    TPMState *tpm_state;

    TPMRecvDataCB *recv_data_callback;
    TPMBackend *tb;
} TPMPassthruThreadParams;

struct TPMPassthruState {
    TPMBackendThread tbt;

    TPMPassthruThreadParams tpm_thread_params;

    char *tpm_dev;
    int tpm_fd;
    bool had_startup_error;
};

#define TPM_PASSTHROUGH_DEFAULT_DEVICE "/dev/tpm0"

/* borrowed from qemu-char.c */
static int tpm_passthrough_unix_write(int fd, const uint8_t *buf, uint32_t len)
{
    return send_all(fd, buf, len);
}

static int tpm_passthrough_unix_read(int fd, uint8_t *buf, uint32_t len)
{
    int ret, len1;
    uint8_t *buf1;

    len1 = len;
    buf1 = buf;
    while ((len1 > 0) && (ret = read(fd, buf1, len1)) != 0) {
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                return -1;
            }
        } else {
            buf1 += ret;
            len1 -= ret;
        }
    }
    return len - len1;
}

static uint32_t tpm_passthrough_get_size_from_buffer(const uint8_t *buf)
{
    return be32_to_cpu(*(uint32_t *)&buf[2]);
}

static int tpm_passthrough_unix_tx_bufs(int tpm_fd,
                                        const uint8_t *in, uint32_t in_len,
                                        uint8_t *out, uint32_t out_len)
{
    int ret;

    ret = tpm_passthrough_unix_write(tpm_fd, in, in_len);
    if (ret != in_len) {
        error_report("tpm_passthrough: error while transmitting data "
                     "to TPM: %s (%i)\n",
                     strerror(errno), errno);
        goto err_exit;
    }

    ret = tpm_passthrough_unix_read(tpm_fd, out, out_len);
    if (ret < 0) {
        error_report("tpm_passthrough: error while reading data from "
                     "TPM: %s (%i)\n",
                     strerror(errno), errno);
    } else if (ret < sizeof(struct tpm_resp_hdr) ||
               tpm_passthrough_get_size_from_buffer(out) != ret) {
        ret = -1;
        error_report("tpm_passthrough: received invalid response "
                     "packet from TPM\n");
    }

err_exit:
    if (ret < 0) {
        tpm_write_fatal_error_response(out, out_len);
    }

    return ret;
}

static int tpm_passthrough_unix_transfer(int tpm_fd,
                                         const TPMLocality *cmd_locty)
{
    return tpm_passthrough_unix_tx_bufs(tpm_fd,
                                        cmd_locty->w_buffer.buffer,
                                        cmd_locty->w_offset,
                                        cmd_locty->r_buffer.buffer,
                                        cmd_locty->r_buffer.size);
}

static void tpm_passthrough_worker_thread(gpointer data,
                                          gpointer user_data)
{
    TPMPassthruThreadParams *thr_parms = user_data;
    TPMPassthruState *tpm_pt = thr_parms->tb->s.tpm_pt;
    TPMBackendCmd cmd = (TPMBackendCmd)data;

    dprintf("tpm_passthrough: processing command type %ld\n", cmd);

    switch (cmd) {
    case TPM_BACKEND_CMD_PROCESS_CMD:
        tpm_passthrough_unix_transfer(tpm_pt->tpm_fd,
                                      thr_parms->tpm_state->cmd_locty);

        thr_parms->recv_data_callback(thr_parms->tpm_state,
                                      thr_parms->tpm_state->command_locty);
        break;
    case TPM_BACKEND_CMD_INIT:
    case TPM_BACKEND_CMD_END:
    case TPM_BACKEND_CMD_TPM_RESET:
        /* nothing to do */
        break;
    }
}

/*
 * Start the TPM (thread). If it had been started before, then terminate
 * and start it again.
 */
static int tpm_passthrough_do_startup_tpm(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    /* terminate a running TPM */
    tpm_backend_thread_end(&tpm_pt->tbt);

    tpm_backend_thread_create(&tpm_pt->tbt,
                              tpm_passthrough_worker_thread,
                              &tb->s.tpm_pt->tpm_thread_params);

    return 0;
}

static int tpm_passthrough_startup_tpm(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;
    int rc;

    rc = tpm_passthrough_do_startup_tpm(tb);
    if (rc) {
        tpm_pt->had_startup_error = true;
    }
    return rc;
}

static void tpm_passthrough_reset(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    dprintf("tpm_passthrough: CALL TO TPM_RESET!\n");

    tpm_backend_thread_end(&tpm_pt->tbt);

    tpm_pt->had_startup_error = false;
}

static int tpm_passthrough_init(TPMBackend *tb, TPMState *s,
                                TPMRecvDataCB *recv_data_cb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    tpm_pt->tpm_thread_params.tpm_state = s;
    tpm_pt->tpm_thread_params.recv_data_callback = recv_data_cb;
    tpm_pt->tpm_thread_params.tb = tb;

    return 0;
}

static bool tpm_passthrough_get_tpm_established_flag(TPMBackend *tb)
{
    return false;
}

static bool tpm_passthrough_get_startup_error(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    return tpm_pt->had_startup_error;
}

static size_t tpm_passthrough_realloc_buffer(TPMSizedBuffer *sb)
{
    size_t wanted_size = 4096; /* Linux tpm.c buffer size */

    if (sb->size != wanted_size) {
        sb->buffer = g_realloc(sb->buffer, wanted_size);
        sb->size = wanted_size;
    }
    return sb->size;
}

static void tpm_passthrough_deliver_request(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    tpm_backend_thread_deliver_request(&tpm_pt->tbt);
}

static const char *tpm_passthrough_create_desc(void)
{
    return "Passthrough TPM backend driver";
}

/*
 * A basic test of a TPM device. We expect a well formatted response header
 * (error response is fine) within one second.
 */
static int tpm_passthrough_test_tpmdev(int fd)
{
    struct tpm_req_hdr req = {
        .tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
        .len = cpu_to_be32(sizeof(req)),
        .ordinal = cpu_to_be32(TPM_ORD_GetTicks),
    };
    struct tpm_resp_hdr *resp;
    fd_set readfds;
    int n;
    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    unsigned char buf[1024];

    n = write(fd, &req, sizeof(req));
    if (n < 0) {
        return errno;
    }
    if (n != sizeof(req)) {
        return EFAULT;
    }

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    /* wait for a second */
    n = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (n != 1) {
        return errno;
    }

    n = read(fd, &buf, sizeof(buf));
    if (n < sizeof(struct tpm_resp_hdr)) {
        return EFAULT;
    }

    resp = (struct tpm_resp_hdr *)buf;
    /* check the header */
    if (be16_to_cpu(resp->tag) != TPM_TAG_RSP_COMMAND ||
        be32_to_cpu(resp->len) != n) {
        return EBADMSG;
    }

    return 0;
}

static int tpm_passthrough_handle_device_opts(QemuOpts *opts, TPMBackend *tb)
{
    const char *value;
    struct stat statbuf;

    value = qemu_opt_get(opts, "fd");
    if (value) {
        if (qemu_opt_get(opts, "path")) {
            error_report("fd= is invalid with path=");
            goto err_exit;
        }

        tb->s.tpm_pt->tpm_fd = qemu_parse_fd(value);
        if (tb->s.tpm_pt->tpm_fd < 0) {
            error_report("Illegal file descriptor for TPM device.\n");
            goto err_exit;
        }

        tb->tpm_fd = &tb->s.tpm_pt->tpm_fd;
    } else {
        value = qemu_opt_get(opts, "path");
        if (!value) {
            value = TPM_PASSTHROUGH_DEFAULT_DEVICE;
        }

        tb->s.tpm_pt->tpm_dev = g_strdup(value);

        tb->path = g_strdup(value);

        tb->s.tpm_pt->tpm_fd = open(tb->s.tpm_pt->tpm_dev, O_RDWR);
        if (tb->s.tpm_pt->tpm_fd < 0) {
            error_report("Cannot access TPM device using '%s'.\n",
                         tb->s.tpm_pt->tpm_dev);
            goto err_free_parameters;
        }
    }

    if (fstat(tb->s.tpm_pt->tpm_fd, &statbuf) != 0) {
        error_report("Cannot determine file descriptor type for TPM "
                     "device: %s", strerror(errno));
        goto err_close_tpmdev;
    }

    /* only allow character devices for now */
    if (!S_ISCHR(statbuf.st_mode)) {
        error_report("TPM file descriptor is not a character device");
        goto err_free_parameters;
    }

    if (tpm_passthrough_test_tpmdev(tb->s.tpm_pt->tpm_fd)) {
        error_report("Device is not a TPM.\n");
        goto err_close_tpmdev;
    }

    return 0;

 err_close_tpmdev:
    close(tb->s.tpm_pt->tpm_fd);
    tb->s.tpm_pt->tpm_fd = -1;

 err_free_parameters:
    g_free(tb->path);
    tb->path = NULL;

    g_free(tb->s.tpm_pt->tpm_dev);
    tb->s.tpm_pt->tpm_dev = NULL;

 err_exit:
    return 1;
}

static TPMBackend *tpm_passthrough_create(QemuOpts *opts, const char *id)
{
    TPMBackend *tb;

    tb = g_new0(TPMBackend, 1);
    tb->s.tpm_pt = g_new0(TPMPassthruState, 1);
    tb->id = g_strdup(id);

    tb->ops = &tpm_passthrough_driver;

    if (tpm_passthrough_handle_device_opts(opts, tb)) {
        goto err_exit;
    }

    return tb;

err_exit:
    g_free(tb->id);
    g_free(tb->s.tpm_pt);
    g_free(tb);

    return NULL;
}

static void tpm_passthrough_destroy(TPMBackend *tb)
{
    TPMPassthruState *tpm_pt = tb->s.tpm_pt;

    tpm_backend_thread_end(&tpm_pt->tbt);

    close(tpm_pt->tpm_fd);

    g_free(tb->id);
    g_free(tb->path);
    g_free(tb->s.tpm_pt->tpm_dev);
    g_free(tb->s.tpm_pt);
    g_free(tb);
}

const TPMDriverOps tpm_passthrough_driver = {
    .id                       = "passthrough",
    .desc                     = tpm_passthrough_create_desc,
    .create                   = tpm_passthrough_create,
    .destroy                  = tpm_passthrough_destroy,
    .init                     = tpm_passthrough_init,
    .startup_tpm              = tpm_passthrough_startup_tpm,
    .realloc_buffer           = tpm_passthrough_realloc_buffer,
    .reset                    = tpm_passthrough_reset,
    .had_startup_error        = tpm_passthrough_get_startup_error,
    .deliver_request          = tpm_passthrough_deliver_request,
    .get_tpm_established_flag = tpm_passthrough_get_tpm_established_flag,
};
