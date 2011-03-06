/*
 * QAPI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include <glib.h>
#include <sys/wait.h>
#include "config-host.h"
#include "libqmp.h"
#include "qerror.h"

#define g_assert_noerr(err) g_assert(err == NULL);
#define g_assert_anyerr(err) g_assert(err != NULL);
#define g_assert_cmperr(err, op, type) do {                   \
    g_assert_anyerr(err);                                        \
    g_assert_cmpstr(error_get_field(err, "class"), op, type); \
} while (0)

static pid_t last_qemu_pid = -1;

static QmpSession *qemu(const char *fmt, ...)
{
    char buffer0[4096];
    char buffer1[4096];
    const char *pid_filename = "/tmp/test-libqmp-qemu.pid";
    const char *path = "/tmp/test-libqmp-qemu.sock";
    struct sockaddr_un addr;
    va_list ap;
    int ret;
    int fd;
    
    va_start(ap, fmt);
    vsnprintf(buffer0, sizeof(buffer0), fmt, ap);
    va_end(ap);

    snprintf(buffer1, sizeof(buffer1),
             "i386-softmmu/qemu "
             "-enable-kvm "
             "-name test-libqmp "
             "-qmp2 qmp "
             "-chardev socket,id=qmp,path=%s,server=on,wait=off "
             "-vnc none "
             "-daemonize "
             "-pidfile %s "
             "%s", path, pid_filename, buffer0);
    g_test_message("Executing %s\n", buffer1);
    ret = system(buffer1);
    g_assert(ret != -1);

    {
        FILE *f;
        char buffer[1024];
        char *ptr;

        f = fopen(pid_filename, "r");
        g_assert(f != NULL);

        ptr = fgets(buffer, sizeof(buffer), f);
        g_assert(ptr != NULL);

        fclose(f);

        last_qemu_pid = atoi(buffer);
    }

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(fd != -1);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    g_assert(ret != -1);

    return qmp_session_new(fd);
}

static void wait_for_pid_exit(pid_t pid)
{
    FILE *f = NULL;

    /* This is ugly but I don't know of a better way */
    do {
        char buffer[1024];

        if (f) {
            fclose(f);
            usleep(10000);
        }

        snprintf(buffer, sizeof(buffer), "/proc/%d/stat", pid);
        f = fopen(buffer, "r");
    } while (f);
}

static void qemu_destroy(QmpSession *sess)
{
    wait_for_pid_exit(last_qemu_pid);
    last_qemu_pid = -1;
    qmp_session_destroy(sess);
}

static void test_version(void)
{
    QmpSession *sess = NULL;
    VersionInfo *info;
    char version[1024];
    char *ptr, *end;
    int major, minor, micro;

    /* Even though we use the same string as the source input, we do parse it
     * a little bit different for no other reason that to make sure we catch
     * potential bugs.
     */
    snprintf(version, sizeof(version), "%s", QEMU_VERSION);
    ptr = version;
    
    end = strchr(ptr, '.');
    g_assert(end != NULL);
    *end = 0;
    major = atoi(ptr);
    ptr = end + 1;

    end = strchr(ptr, '.');
    g_assert(end != NULL);
    *end = 0;
    minor = atoi(ptr);
    ptr = end + 1;
    
    micro = atoi(ptr);
    while (g_ascii_isdigit(*ptr)) ptr++;

    sess = qemu("-S");

    info = libqmp_query_version(sess, NULL);

    g_assert_cmpint(major, ==, info->qemu.major);
    g_assert_cmpint(minor, ==, info->qemu.minor);
    g_assert_cmpint(micro, ==, info->qemu.micro);
    g_assert_cmpstr(ptr, ==, info->package);

    qmp_free_version_info(info);

    libqmp_quit(sess, NULL);

    qemu_destroy(sess);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/0.14/misc/version", test_version);

    g_test_run();

    return 0;
}
