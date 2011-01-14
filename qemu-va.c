/*
 * virtagent - QEMU guest agent
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <getopt.h>
#include <err.h>
#include "qemu-ioh.h"
#include "qemu-tool.h"
#include "virtagent-common.h"

static bool verbose_enabled;
#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
#define DEBUG(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)
#else
#define DEBUG(msg, ...) do {} while (0)
#endif

#define INFO(msg, ...) do { \
    if (!verbose_enabled) { \
        break; \
    } \
    warnx(msg, ## __VA_ARGS__); \
} while(0)

/* mirror qemu I/O loop for standalone daemon */
static void main_loop_wait(int nonblocking)
{
    fd_set rfds, wfds, xfds;
    int ret, nfds;
    struct timeval tv;
    int timeout = 100000;

    if (nonblocking) {
        timeout = 0;
    }

    /* poll any events */
    nfds = -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    qemu_get_fdset(&nfds, &rfds, &wfds, &xfds);

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);

    if (ret > 0) {
        qemu_process_fd_handlers(&rfds, &wfds, &xfds);
    }

    DEBUG("running timers...");
    qemu_run_all_timers();
}

static void usage(const char *cmd)
{
    printf(
"Usage: %s -c <channel_opts>\n"
"QEMU virtagent guest agent %s\n"
"\n"
"  -c, --channel     channel method: one of unix-connect, virtio-serial, or\n"
"                    isa-serial\n"
"  -p, --path        channel path\n"
"  -v, --verbose     display extra debugging information\n"
"  -d, --daemonize   become a daemon\n"
"  -h, --help        display this help and exit\n"
"\n"
"Report bugs to <mdroth@linux.vnet.ibm.com>\n"
    , cmd, VA_VERSION);
}

static int init_virtagent(const char *method, const char *path) {
    VAContext ctx;
    int ret;

    INFO("initializing agent...");

    if (method == NULL) {
        /* try virtio-serial as our default */
        method = "virtio-serial";
    }

    if (path == NULL) {
        if (strcmp(method, "virtio-serial")) {
            errx(EXIT_FAILURE, "must specify a path for this channel");
        }
        /* try the default name for the virtio-serial port */
        path = VA_GUEST_PATH_VIRTIO_DEFAULT;
    }

    /* initialize virtagent */
    ctx.is_host = false;
    ctx.channel_method = method;
    ctx.channel_path = path;
    ret = va_init(ctx);
    if (ret) {
        errx(EXIT_FAILURE, "unable to initialize virtagent");
    }

    return 0;
}

static void become_daemon(void)
{
    pid_t pid, sid;

    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0) {
        FILE *pidfile = fopen(VA_PIDFILE, "wx");
        if (!pidfile)
            errx(EXIT_FAILURE, "Error creating pid file");
        fprintf(pidfile, "%i", pid);
        fclose(pidfile);
        exit(EXIT_SUCCESS);
    }

    umask(0);
    sid = setsid();
    if (sid < 0)
        goto fail;
    if ((chdir("/")) < 0)
        goto fail;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    return;

fail:
    unlink(VA_PIDFILE);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    const char *sopt = "hVvdc:p:", *channel_method = NULL, *channel_path = NULL;
    struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "verbose", 0, NULL, 'v' },
        { "channel", 0, NULL, 'c' },
        { "path", 0, NULL, 'p' },
        { "daemonize", 0, NULL, 'd' },
        { NULL, 0, NULL, 0 }
    };
    int opt_ind = 0, ch, ret, daemonize = 0;

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'c':
            channel_method = optarg;
            break;
        case 'p':
            channel_path = optarg;
            break;
        case 'v':
            verbose_enabled = 1;
            break;
        case 'V':
            printf("QEMU Virtagent %s\n", VA_VERSION);
            return 0;
        case 'd':
            daemonize = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        case '?':
            errx(EXIT_FAILURE, "Try '%s --help' for more information.",
                 argv[0]);
        }
    }

    init_clocks();
    configure_alarms("dynticks");
    if (init_timer_alarm() < 0) {
        errx(EXIT_FAILURE, "could not initialize alarm timer");
    }

    /* initialize virtagent */
    ret = init_virtagent(channel_method, channel_path);
    if (ret) {
        errx(EXIT_FAILURE, "error initializing communication channel");
    }

    /* tell the host the agent is running */
    va_send_hello();

    if (daemonize) {
        become_daemon();
    }

    /* main i/o loop */
    for (;;) {
        DEBUG("entering main_loop_wait()");
        main_loop_wait(0);
        DEBUG("left main_loop_wait()");
    }

    unlink(VA_PIDFILE);
    return 0;
}
