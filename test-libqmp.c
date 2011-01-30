#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include <glib.h>
#include "config-host.h"
#include "libqmp.h"
#include "qerror.h"

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
    vsnprintf(buffer0, sizeof(buffer0), fmt, ap);
    va_end(ap);

    snprintf(buffer1, sizeof(buffer1), "./qemu-img %s >/dev/null", buffer0);

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
             "i386-softmmu/qemu "
             "-enable-kvm "  // glib series breaks TCG
             "-qmp2 qmp "
             "-chardev socket,id=qmp,path=\"%s\",server=on,wait=off "
             "-vnc none "
             "-daemonize "
             "%s", filename, buffer0);
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

static void read_or_assert(int fd, void *buffer, size_t size)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        len = read(fd, buffer + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        g_assert(len != 0);
        g_assert(len != -1);

        offset += len;
    }
}

#if 0
static void write_or_assert(int fd, const void *buffer, size_t size)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        len = write(fd, buffer + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        g_assert(len < 1);

        offset += len;
    }
}
#endif

static int vnc_connect(int port, const char *password)
{
    char greeting[13];
    struct sockaddr_in addr;
    struct in_addr in;
    int s, ret;

    ret = inet_aton("127.0.0.1", &in);
    g_assert(ret != 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, &in, sizeof(in));

    s = socket(PF_INET, SOCK_STREAM, 0);
    g_assert(s != -1);

    ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1 && errno == ECONNREFUSED) {
        close(s);
        return -ECONNREFUSED;
    }

    read_or_assert(s, greeting, 12);
    greeting[12] = 0;

    g_assert_cmpstr(greeting, ==, "RFB 003.008\n");

    close(s);

    return 0;
}

static void test_vnc_change(void)
{
    QmpSession *sess;
    int ret;
    Error *err = NULL;

    sess = qemu("-S -vnc :300");
    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 301, NULL);
    g_assert_cmpint(ret, ==, -ECONNREFUSED);

    libqmp_change_vnc_listen(sess, ":301", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 301, NULL);
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -ECONNREFUSED);

    libqmp_quit(sess, NULL);
    qmp_session_destroy(sess);
}

static void test_change_block_autoprobe(void)
{
    QmpSession *sess;
    const char *filename = "/tmp/foo.raw";
    Error *err = NULL;

    qemu_img("create -f raw %s 10G", filename);
    sess = qemu("-S");

    libqmp_change_blockdev(sess, "ide1-cd0", filename, false, NULL,
                           false, NULL, &err);
    g_assert(err != NULL);
    g_assert_cmpstr("MissingParameter", ==, error_get_field(err, "class"));
    error_free(err);

    err = NULL;
    libqmp_change_blockdev(sess, "ide1-cd0", filename,
                           false, NULL, true, "raw", &err);
    g_assert(err == NULL);

    libqmp_quit(sess, NULL);
    unlink(filename);
    qmp_session_destroy(sess);
}

static void test_change_block_encrypted(void)
{
    QmpSession *sess;
    const char *filename = "/tmp/foo.qcow2";
    Error *err = NULL;

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S");
    libqmp_change_blockdev(sess, "ide1-cd0", filename, false, NULL,
                           false, NULL, &err);
    g_assert(err != NULL);
    g_assert(error_is_type(err, QERR_DEVICE_ENCRYPTED));
    error_free(err);

    err = NULL;
    libqmp_block_passwd(sess, "ide1-cd0", "foo", &err);
    g_assert(err != NULL);
    g_assert(error_is_type(err, QERR_DEVICE_NOT_ENCRYPTED));
    error_free(err);

    err = NULL;
    libqmp_change_blockdev(sess, "ide1-cd0", filename, true, "foo",
                           false, NULL, &err);
    g_assert(err == NULL);

    libqmp_quit(sess, NULL);
    unlink(filename);
    qmp_session_destroy(sess);
}

static void test_change_old_block_encrypted(void)
{
    QmpSession *sess;
    const char *filename = "/tmp/foo.qcow2";
    Error *err = NULL;

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S");
    libqmp_change(sess, "ide1-cd0", filename, false, NULL, &err);
    g_assert(err != NULL);
    g_assert(error_is_type(err, QERR_DEVICE_ENCRYPTED));
    error_free(err);

    err = NULL;
    libqmp_block_passwd(sess, "ide1-cd0", "foo", &err);
    g_assert(err == NULL);

    libqmp_quit(sess, NULL);
    unlink(filename);
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

    qmp_free_VersionInfo(info);

    libqmp_quit(sess, NULL);

    qmp_session_destroy(sess);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/misc/version", test_version);
    g_test_add_func("/block/change/encrypted", test_change_block_encrypted);
    g_test_add_func("/block/change/autoprobe", test_change_block_autoprobe);
    g_test_add_func("/deprecated/block/change/encrypted",
                    test_change_old_block_encrypted);
    g_test_add_func("/vnc/change", test_vnc_change);

    g_test_run();

    return 0;
}
