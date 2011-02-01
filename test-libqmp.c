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
#include "ui/d3des.h"

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

static pid_t last_qemu_pid = -1;

static QmpSession *qemu(const char *fmt, ...)
{
    char buffer0[4096];
    char buffer1[4096];
    const char *filename = "foo.sock";
    const char *pid_filename = "/tmp/test-libqmp-qemu.pid";
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
#if 1
             "-qmp2 qmp "
             "-chardev socket,id=qmp,path=\"%s\",server=on,wait=off "
#else
             "-qmp unix:\"%s\",server,nowait "
#endif
             "-vnc none "
             "-daemonize "
             "-pidfile %s "
             "%s", filename, pid_filename, buffer0);
    g_test_message("Executing %s\n", buffer1);
    ret = system(buffer1);
    g_assert(ret != -1);

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(s != -1);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", filename);

    ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
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

    return qmp_session_new(s);
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

static void write_or_assert(int fd, const void *buffer, size_t size)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        len = write(fd, buffer + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        g_assert(len != 0);
        g_assert(len != -1);

        offset += len;
    }
}

static int vnc_connect(int port, const char *password)
{
    char greeting[13];
    struct sockaddr_in addr;
    struct in_addr in;
    int s, ret;
    uint8_t num_auth;
    uint8_t auth;

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

    write_or_assert(s, greeting, 12);

    read_or_assert(s, &num_auth, sizeof(num_auth));
    g_assert_cmpuint(num_auth, ==, 1);

    read_or_assert(s, &auth, sizeof(auth));
    if (auth == 2) {
        uint8_t challenge[16];
        uint8_t key[8];
        uint32_t result;
        int pwlen;

        if (password == NULL) {
            close(s);
            return -EPERM;
        }
        write_or_assert(s, &auth, sizeof(auth));
        read_or_assert(s, challenge, sizeof(challenge));

        pwlen = strlen(password);
        memset(key, 0, sizeof(key));
        memcpy(key, password, MIN(pwlen, 8));

        deskey(key, EN0);
        des(challenge, challenge);
        des(challenge + 8, challenge + 8);

        write_or_assert(s, challenge, sizeof(challenge));
        read_or_assert(s, &result, sizeof(result));

        if (result != 0) {
            close(s);
            return -EPERM;
        }
    } else if (auth != 1) {
        close(s);
        return -EINVAL;
    }

    close(s);

    return 0;
}

static void test_vnc_password(void)
{
    QmpSession *sess;
    int ret;
    Error *err = NULL;

    sess = qemu("-S -vnc :300,password");
    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "hello world");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change_vnc_password(sess, "foobar", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, "bob");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change_vnc_password(sess, "monkey", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "monkey");
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change_vnc_password(sess, "", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, "foo");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
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
    qemu_destroy(sess);
}

static void test_deprecated_vnc_password(void)
{
    QmpSession *sess;
    int ret;
    Error *err = NULL;

    sess = qemu("-S -vnc :300,password");
    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change(sess, "vnc", "password", true, "foobar", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, "monkey");
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, 0);

    libqmp_change(sess, "vnc", "password", true, "", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change(sess, "vnc", "password", true, "", &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change(sess, "vnc", "password", false, NULL, &err);
    g_assert(err == NULL);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
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
    qemu_destroy(sess);
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
    qemu_destroy(sess);
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
    qemu_destroy(sess);
}

static void test_cont(void)
{
    QmpSession *sess;
    Error *err;
    const char *filename = "/tmp/foo.qcow2";

    sess = qemu("-S");
    libqmp_cont(sess, &err);
    g_assert(err == NULL);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    sess = qemu("-S -incoming tcp:localhost:6100");
    libqmp_cont(sess, &err);
    g_assert(err != NULL);
    g_assert(error_is_type(err, QERR_MIGRATION_EXPECTED));
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S -hda %s", filename);
    libqmp_cont(sess, &err);
    g_assert(err != NULL);
    /* Can't test for a specific type here as 0.14 sent a generic error */
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
    unlink(filename);
}

static void test_stop(void)
{
    QmpSession *sess;
    Error *err = NULL;

    sess = qemu("");
    libqmp_stop(sess, &err);
    g_assert(err == NULL);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_screendump(void)
{
    QmpSession *sess = NULL;
    const char *filename = "/tmp/test-libqmp.screendump";
    Error *err = NULL;
    int ret;

    unlink(filename);

    ret = access(filename, F_OK);
    g_assert_cmpint(ret, ==, -1);

    sess = qemu("");
    /* There is some whacky race here with QEMU.  More investigation
       is needed */
    sleep(1);
    libqmp_screendump(sess, filename, &err);
    g_assert(err == NULL);

    ret = access(filename, F_OK);
    g_assert_cmpint(ret, ==, 0);
    unlink(filename);

    libqmp_screendump(sess, "/tmp/path/to/directory/thats/clearly/not/there",
                      &err);
    g_assert(err != NULL);
    g_assert_cmpstr(error_get_field(err, "class"), ==, "OpenFileFailed");
    error_free(err);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_block_query(void)
{
    QmpSession *sess = NULL;
    BlockInfo *block_list, *info;
    Error *err = NULL;
    bool found_cd, found_floppy, found_hd1;
    const char *filename = "/tmp/foo.qcow2";

    sess = qemu("-S");

    block_list = libqmp_query_block(sess, &err);
    g_assert(err == NULL);

    found_cd = found_floppy = found_hd1 = false;
    for (info = block_list; info; info = info->next) {
        if (strcmp(info->device, "ide1-cd0") == 0) {
            found_cd = true;
            g_assert_cmpint(info->locked, ==, false);
            g_assert_cmpint(info->has_inserted, ==, false);
            g_assert_cmpstr(info->type, ==, "cdrom");
            g_assert_cmpint(info->removable, ==, true);
        } else if (strcmp(info->device, "floppy0") == 0) {
            found_floppy = true;
            g_assert_cmpint(info->locked, ==, false);
            g_assert_cmpint(info->has_inserted, ==, false);
            g_assert_cmpstr(info->type, ==, "floppy");
            g_assert_cmpint(info->removable, ==, true);
        }
    }

    g_assert(found_cd);
    g_assert(found_floppy);

    qmp_free_BlockInfo(block_list);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S -hda %s", filename);

    block_list = libqmp_query_block(sess, &err);
    g_assert(err == NULL);

    found_cd = found_floppy = found_hd1 = false;
    for (info = block_list; info; info = info->next) {
        if (strcmp(info->device, "ide0-hd0") == 0) {
            found_hd1 = true;
            g_assert_cmpint(info->locked, ==, false);

            g_assert_cmpint(info->has_inserted, ==, true);
            g_assert_cmpstr(info->inserted.file, ==, filename);
            g_assert_cmpint(info->inserted.ro, ==, false);
            g_assert_cmpstr(info->inserted.drv, ==, "qcow2");
            g_assert_cmpint(info->inserted.encrypted, ==, true);
            g_assert_cmpint(info->inserted.has_backing_file, ==, false);

            g_assert_cmpstr(info->type, ==, "hd");
            g_assert_cmpint(info->removable, ==, false);
        } else if (strcmp(info->device, "ide1-cd0") == 0) {
            found_cd = true;
            g_assert_cmpint(info->locked, ==, false);
            g_assert_cmpint(info->has_inserted, ==, false);
            g_assert_cmpstr(info->type, ==, "cdrom");
            g_assert_cmpint(info->removable, ==, true);
        } else if (strcmp(info->device, "floppy0") == 0) {
            found_floppy = true;
            g_assert_cmpint(info->locked, ==, false);
            g_assert_cmpint(info->has_inserted, ==, false);
            g_assert_cmpstr(info->type, ==, "floppy");
            g_assert_cmpint(info->removable, ==, true);
        }
    }

    g_assert(found_hd1);
    g_assert(found_cd);
    g_assert(found_floppy);

    qmp_free_BlockInfo(block_list);

    unlink(filename);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
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

    qemu_destroy(sess);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/0.14/misc/version", test_version);
    g_test_add_func("/0.14/block/change/encrypted",
                    test_change_old_block_encrypted);
    g_test_add_func("/0.14/vnc/password-login",
                    test_deprecated_vnc_password);
    g_test_add_func("/0.14/display/screendump",
                    test_screendump);
    g_test_add_func("/0.14/misc/cont", test_cont);
    g_test_add_func("/0.14/misc/stop", test_stop);
    g_test_add_func("/0.14/block/query", test_block_query);

    g_test_add_func("/0.15/vnc/change", test_vnc_change);
    g_test_add_func("/0.15/block/change/encrypted",
                    test_change_block_encrypted);
    g_test_add_func("/0.15/block/change/autoprobe",
                    test_change_block_autoprobe);
    g_test_add_func("/0.15/vnc/password-login",
                    test_vnc_password);

    g_test_run();

    return 0;
}
