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

#define g_assert_noerr(err) g_assert(err == NULL);
#define g_assert_anyerr(err) g_assert(err != NULL);
#define g_assert_cmperr(err, op, type) do {                   \
    g_assert_anyerr(err);                                        \
    g_assert_cmpstr(error_get_field(err, "class"), op, type); \
} while (0)

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

#define QMP2_CHARDEV
//#define QMP2_UNIX
//#define QMP_NORMAL

static QmpSession *qemu(const char *fmt, ...)
{
    char buffer0[4096];
    char buffer1[4096];
    const char *pid_filename = "/tmp/test-libqmp-qemu.pid";
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    vsnprintf(buffer0, sizeof(buffer0), fmt, ap);
    va_end(ap);

    snprintf(buffer1, sizeof(buffer1),
             "i386-softmmu/qemu "
             "-enable-kvm "  // glib series breaks TCG
             "-name test-libqmp "
             "-vnc none "
             "-daemonize "
             "-pidfile %s "
             "%s", pid_filename, buffer0);
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

    return libqmp_session_new_name("test-libqmp");
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
    g_assert_noerr(err);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, "bob");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change_vnc_password(sess, "monkey", &err);
    g_assert_noerr(err);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "monkey");
    g_assert_cmpint(ret, ==, 0);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change_vnc_password(sess, "", &err);
    g_assert_noerr(err);

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
    g_assert_noerr(err);

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
    g_assert_noerr(err);

    ret = vnc_connect(5900 + 300, "monkey");
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "foobar");
    g_assert_cmpint(ret, ==, 0);

    libqmp_change(sess, "vnc", "password", true, "", &err);
    g_assert_noerr(err);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change(sess, "vnc", "password", true, "", &err);
    g_assert_noerr(err);

    ret = vnc_connect(5900 + 300, NULL);
    g_assert_cmpint(ret, ==, -EPERM);

    ret = vnc_connect(5900 + 300, "");
    g_assert_cmpint(ret, ==, -EPERM);

    libqmp_change(sess, "vnc", "password", false, NULL, &err);
    g_assert_noerr(err);

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
    g_assert_cmperr(err, ==, "MissingParameter");
    error_free(err);

    err = NULL;
    libqmp_change_blockdev(sess, "ide1-cd0", filename,
                           false, NULL, true, "raw", &err);
    g_assert_noerr(err);

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
    g_assert_cmperr(err, ==, "DeviceEncrypted");
    error_free(err);

    err = NULL;
    libqmp_block_passwd(sess, "ide1-cd0", "foo", &err);
    g_assert_cmperr(err, ==, "DeviceNotEncrypted");
    error_free(err);

    err = NULL;
    libqmp_change_blockdev(sess, "ide1-cd0", filename, true, "foo",
                           false, NULL, &err);
    g_assert_noerr(err);

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
    g_assert_cmperr(err, ==, "DeviceEncrypted");
    error_free(err);

    err = NULL;
    libqmp_block_passwd(sess, "ide1-cd0", "foo", &err);
    g_assert_noerr(err);

    libqmp_quit(sess, NULL);
    unlink(filename);
    qemu_destroy(sess);
}

static void test_running(QmpSession *sess)
{
    StatusInfo *info;
    Error *err = NULL;

    info = libqmp_query_status(sess, &err);
    g_assert_noerr(err);

    g_assert_cmpint(info->running, ==, true);
    qmp_free_status_info(info);
}

static void test_stopped(QmpSession *sess)
{
    StatusInfo *info;
    Error *err = NULL;

    info = libqmp_query_status(sess, &err);
    g_assert_noerr(err);

    g_assert_cmpint(info->running, ==, false);
    qmp_free_status_info(info);
}

static void test_cont(void)
{
    QmpSession *sess;
    Error *err;
    const char *filename = "/tmp/foo.qcow2";

    sess = qemu("-S");
    test_stopped(sess);
    libqmp_cont(sess, &err);
    g_assert_noerr(err);
    test_running(sess);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    sess = qemu("-S -incoming tcp:localhost:6100");
    test_stopped(sess);
    libqmp_cont(sess, &err);
    g_assert_cmperr(err, ==, "MigrationExpected");
    error_free(err);
    test_stopped(sess);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S -hda %s", filename);
    test_stopped(sess);
    libqmp_cont(sess, &err);
    g_assert_anyerr(err);
    error_free(err);
    test_stopped(sess);
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
    test_running(sess);
    libqmp_stop(sess, &err);
    g_assert_noerr(err);
    test_stopped(sess);
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
    g_assert_noerr(err);

    ret = access(filename, F_OK);
    g_assert_cmpint(ret, ==, 0);
    unlink(filename);

    libqmp_screendump(sess, "/tmp/path/to/directory/thats/clearly/not/there",
                      &err);
    g_assert_cmperr(err, ==, "OpenFileFailed");
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
    g_assert_noerr(err);

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

    qmp_free_block_info(block_list);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);

    qemu_img("create -f qcow2 -o encryption %s 10G", filename);
    sess = qemu("-S -hda %s", filename);

    block_list = libqmp_query_block(sess, &err);
    g_assert_noerr(err);

    found_cd = found_floppy = found_hd1 = false;
    for (info = block_list; info; info = info->next) {
        if (strcmp(info->device, "ide0-hd0") == 0) {
            found_hd1 = true;
            g_assert_cmpint(info->locked, ==, false);

            g_assert_cmpint(info->has_inserted, ==, true);
            g_assert_cmpstr(info->inserted->file, ==, filename);
            g_assert_cmpint(info->inserted->ro, ==, false);
            g_assert_cmpstr(info->inserted->drv, ==, "qcow2");
            g_assert_cmpint(info->inserted->encrypted, ==, true);
            g_assert_cmpint(info->inserted->has_backing_file, ==, false);

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

    qmp_free_block_info(block_list);

    unlink(filename);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_block_query_stats(void)
{
    QmpSession *sess = NULL;
    BlockStats *stats_list, *stats;
    Error *err = NULL;
    bool found_cd, found_floppy, found_hd1;
    const char *filename = "/tmp/test-libqmp.qcow2";

    qemu_img("create -f qcow2 %s 10G", filename);
    sess = qemu("-hda %s", filename);

    stats_list = libqmp_query_blockstats(sess, &err);
    g_assert_noerr(err);

    found_cd = found_floppy = found_hd1 = false;
    for (stats = stats_list; stats; stats = stats->next) {
        g_assert(stats->has_device == true);
        if (strcmp(stats->device, "ide1-cd0") == 0) {
            found_cd = true;
            g_assert(stats->has_parent == false);
        } else if (strcmp(stats->device, "floppy0") == 0) {
            found_floppy = true;
            g_assert(stats->has_parent == false);
        } else if (strcmp(stats->device, "ide0-hd0") == 0) {
            found_hd1 = true;
            g_assert(stats->has_parent == true);
            g_assert(stats->parent->has_device == false);
            g_assert(stats->parent->has_parent == false);
        }
    }

    g_assert(found_cd);
    g_assert(found_floppy);
    g_assert(found_hd1);

    qmp_free_block_stats(stats_list);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
    unlink(filename);
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

static void test_device_add_bad_id(void)
{
    QmpSession *sess;
    Error *err = NULL;

    sess = qemu("-S");

    libqmp_device_add(sess, "virtio-blk-pci", "32", NULL, &err);
    g_assert_cmperr(err, ==, "InvalidParameterValue");
    g_assert_cmpstr(error_get_field(err, "name"), ==, "id");
    error_free(err);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_add_bad_driver(void)
{
    QmpSession *sess;
    Error *err = NULL;

    sess = qemu("-S");

    libqmp_device_add(sess, "no-such-device", "bleh", NULL, &err);
    g_assert_cmperr(err, ==, "InvalidParameterValue");
    g_assert_cmpstr(error_get_field(err, "name"), ==, "driver");
    error_free(err);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static KeyValues *kv_alloc(const char *key, ...)
{
    KeyValues *kv_list = NULL;
    va_list ap;

    va_start(ap, key);
    while (key) {
        KeyValues *kv = qmp_alloc_key_values();
        kv->key = qemu_strdup(key);
        kv->value = qemu_strdup(va_arg(ap, const char *));
        kv->next = kv_list;
        kv_list = kv;
        key = va_arg(ap, const char *);
    }
    va_end(ap);

    return kv_list;
}

static void test_device_add_bad_param(void)
{
    QmpSession *sess;
    Error *err = NULL;
    KeyValues *kv;

    sess = qemu("-S");

    kv = kv_alloc("not-a-valid-parameter-name", "value", NULL);
    libqmp_device_add(sess, "virtio-blk-pci", "bleh", kv, &err);
    g_assert_cmperr(err, ==, "PropertyNotFound");
    g_assert_cmpstr(error_get_field(err, "property"), ==, "not-a-valid-parameter-name");
    error_free(err);

    qmp_free_key_values(kv);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_add_bad_param_value(void)
{
    QmpSession *sess;
    Error *err = NULL;
    KeyValues *kv;

    sess = qemu("-S");

    kv = kv_alloc("addr", "bolagna", NULL);
    libqmp_device_add(sess, "virtio-blk-pci", "bleh", kv, &err);
    g_assert_cmperr(err, ==, "PropertyValueBad");
    g_assert_cmpstr(error_get_field(err, "property"), ==, "addr");
    error_free(err);

    qmp_free_key_values(kv);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_add_not_found_param_value(void)
{
    QmpSession *sess;
    Error *err = NULL;
    KeyValues *kv;

    sess = qemu("-S");

    kv = kv_alloc("drive", "bolagna", NULL);
    libqmp_device_add(sess, "virtio-blk-pci", "bleh", kv, &err);
    g_assert_cmperr(err, ==, "PropertyValueNotFound");
    g_assert_cmpstr(error_get_field(err, "property"), ==, "drive");
    error_free(err);

    qmp_free_key_values(kv);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_add_nic(void)
{
    QmpSession *sess;
    Error *err = NULL;
    KeyValues *kv;

    sess = qemu("-S -netdev user,id=netdev0 2> /dev/null");
    
    kv = kv_alloc("netdev", "netdev0", NULL);

    libqmp_device_add(sess, "virtio-net-pci", "net0", kv, &err);
    g_assert_noerr(err);

    qmp_free_key_values(kv);
    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_del_bad_id(void)
{
    QmpSession *sess;
    Error *err = NULL;

    sess = qemu("-S -netdev user,id=foo -device virtio-net-pci,id=net0,netdev=foo");

    libqmp_device_del(sess, "not-a-valid-device-name", &err);
    g_assert_cmperr(err, ==, "DeviceNotFound");
    error_free(err);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_device_del_nic(void)
{
    QmpSession *sess;
    Error *err = NULL;
    KeyValues *kv;

    sess = qemu("-S -netdev user,id=netdev0 2> /dev/null");
    
    kv = kv_alloc("netdev", "netdev0", NULL);

    libqmp_device_add(sess, "virtio-net-pci", "net0", kv, &err);
    g_assert_noerr(err);
    qmp_free_key_values(kv);

    libqmp_device_del(sess, "net0", &err);
    g_assert_noerr(err);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_query_network(void)
{
    QmpSession *sess;
    Error *err = NULL;
    NetworkInfo *info_list, *info;
    bool found_slirp0 = false;
    bool found_slirp1 = false;
    bool found_nic0 = false;
    bool found_nic1 = false;

    sess = qemu("-S "
                "-net user,id=slirp.0,vlan=1 -device e1000,id=nic.0,vlan=1 "
                "-netdev user,id=slirp.1 -device e1000,id=nic.1,netdev=slirp.1 ");
    
    info_list = libqmp_query_network(sess, &err);
    g_assert_noerr(err);

    for (info = info_list; info; info = info->next) {
        if (strcmp(info->name, "slirp.0") == 0) {
            found_slirp0 = true;
            g_assert_cmpint(info->type, ==, NT_SLIRP);
            g_assert(info->has_vlan_id == true);
            g_assert_cmpint(info->vlan_id, ==, 1);
            g_assert(info->has_peer == false);
        } else if (strcmp(info->name, "slirp.1") == 0) {
            found_slirp1 = true;
            g_assert_cmpint(info->type, ==, NT_SLIRP);
            g_assert(info->has_vlan_id == false);
            g_assert(info->has_peer == true);
            g_assert_cmpstr(info->peer, ==, "nic.1");
        } else if (strcmp(info->name, "nic.0") == 0) {
            found_nic0 = true;
            g_assert_cmpint(info->type, ==, NT_NIC);
            g_assert(info->has_vlan_id == true);
            g_assert_cmpint(info->vlan_id, ==, 1);
            g_assert(info->has_peer == false);
        } else if (strcmp(info->name, "nic.1") == 0) {
            found_nic1 = true;
            g_assert_cmpint(info->type, ==, NT_NIC);
            g_assert(info->has_vlan_id == false);
            g_assert(info->has_peer == true);
            g_assert_cmpstr(info->peer, ==, "slirp.1");
        }
    }

    g_assert(found_slirp0 == true);
    g_assert(found_slirp1 == true);
    g_assert(found_nic0 == true);
    g_assert(found_nic1 == true);

    qmp_free_network_info(info_list);

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_query_tap(void)
{
    QmpSession *sess;
    Error *err = NULL;
    TapInfo *tap;

    sess = qemu("-S -net tap,script=,ifname=tap100,id=foo");
    
    tap = libqmp_query_tap(sess, "foo", &err);
    g_assert_noerr(err);

    printf("fd: %ld\n", tap->fd);
    if (tap->down_script) {
        printf("down script: %s\n", tap->down_script);
    }
    if (tap->down_script_arg) {
        printf("down script arg: %s\n", tap->down_script_arg);
    }
    printf("options:");
    if (tap->vnet_hdr_enabled) {
        printf(" vnet");
    }
    printf("\n");

    libqmp_quit(sess, NULL);
    qemu_destroy(sess);
}

static void test_query_pci(void)
{
    QmpSession *sess;
    Error *err = NULL;
    PciInfo *pci_busses, *info;
    bool found_balloon = false;

    sess = qemu("-S -device virtio-balloon-pci,id=foo,addr=4.0");
    pci_busses = libqmp_query_pci(sess, &err);
    g_assert_noerr(err);

    for (info = pci_busses; info; info = info->next) {
        PciDeviceInfo *dev;

        for (dev = info->devices; dev; dev = dev->next) {
            if (dev->slot == 4 && dev->function == 0) {
                found_balloon = true;
                g_assert_cmpstr(dev->qdev_id, ==, "foo");
                g_assert(dev->class_info.has_desc == true);
                g_assert_cmpstr(dev->class_info.desc, ==, "RAM controller");
                g_assert_cmpint(dev->id.vendor, ==, 0x1af4);
                g_assert_cmpint(dev->id.device, ==, 0x1002);
            }
        }
    }

    g_assert(found_balloon == true);

    qmp_free_pci_info(pci_busses);

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
    g_test_add_func("/0.14/block/query/stats", test_block_query_stats);
    g_test_add_func("/0.14/device-add/err/id", test_device_add_bad_id);
    g_test_add_func("/0.14/device-add/err/driver", test_device_add_bad_driver);
    g_test_add_func("/0.14/device-add/err/param", test_device_add_bad_param);
    g_test_add_func("/0.14/device-add/err/param-value", test_device_add_bad_param_value);
    g_test_add_func("/0.14/device-add/err/value-not-found",
                    test_device_add_not_found_param_value);
    g_test_add_func("/0.14/device-add/nic", test_device_add_nic);
    g_test_add_func("/0.14/device-del/err/id", test_device_del_bad_id);
    g_test_add_func("/0.14/device-del/nic", test_device_del_nic);
    g_test_add_func("/0.14/pci/query", test_query_pci);

    g_test_add_func("/0.15/vnc/change", test_vnc_change);
    g_test_add_func("/0.15/block/change/encrypted",
                    test_change_block_encrypted);
    g_test_add_func("/0.15/block/change/autoprobe",
                    test_change_block_autoprobe);
    g_test_add_func("/0.15/vnc/password-login",
                    test_vnc_password);
    g_test_add_func("/0.15/net/query", test_query_network);
    if (geteuid() == 0) {
        g_test_add_func("/0.15/net/query/tap", test_query_tap);
    }

    g_test_run();

    return 0;
}
