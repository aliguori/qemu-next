#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <glib.h>
#include "config-host.h"
#include "libqmp.h"

/**
 * TODO:
 *  1) make a QmpSession that actually works
 *  2) split the type, alloc, and free functions into a separate header/file
 *     such that we don't have to expose qobject in the library interface
 *  3) figure out how to support errors without exposing qobject
 */

static void qemu_img(const char *fmt, ...)
{
    char buffer0[4096];
    char buffer1[4096];
    va_list ap;
    int ret;

    va_start(ap, fmt);
    vnsprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    snprintf(buffer1, sizeof(buffer1), "./qemu-img %s", buffer0);

    ret = system(buffer1);
    g_assert(ret != -1);
}

static QmpSession *qemu(const char *fmt, ...)
{
    char buffer0[4096];
    char buffer1[4096];
    const char *filename = "foo.sock";
    struct sockaddr_un addr;
    va_list ap;
    int s;
    int ret;
    
    va_start(ap, fmt);
    vsnprintf(buffer0, sizeof(buffer0), fmt, ap);
    va_end(ap);

    snprintf(buffer1, sizeof(buffer1),
             "i386-softmmu/qemu %s "
             "-qmp2 qmp "
             "-chardev socket,id=qmp,path=\"%s\",server=on,wait=off "
             "-vnc none "
             "-daemonize", buffer0, filename);
    ret = system(buffer1);
    g_assert(ret != -1);

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(s != -1);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", filename);

    ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    g_assert(ret != -1);

    return qmp_session_new(s);
}

static void test_change_block_encrypted(void)
{
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

    sess = qemu("-S -enable-kvm");

    info = libqmp_query_version(sess, NULL);

    g_assert_cmpint(major, ==, info->qemu.major);
    g_assert_cmpint(minor, ==, info->qemu.minor);
    g_assert_cmpint(micro, ==, info->qemu.micro);
    g_assert_cmpstr(ptr, ==, info->package);

    qmp_free_VersionInfo(info);

    libqmp_quit(sess, NULL);

    qmp_session_destroy(sess);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/misc/version", test_version);
    g_test_add_func("/block/change-encrypted", test_change_block_encrypted);

    g_test_run();

    return 0;
}
