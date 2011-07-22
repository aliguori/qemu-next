/*
 * QEMU Coverage Tester
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>
#include <linux/iso_fs.h>

static uint8_t iso_u8(void *ptr)
{
    uint8_t *buf = (uint8_t *)ptr;
    return buf[0];
}

static inline uint16_t iso_u16(void *ptr)
{
    uint8_t *buf = (uint8_t *)ptr;
    return buf[0] | (buf[1] << 8);
}

static inline uint32_t iso_u32(void *ptr)
{
    uint8_t *buf = (uint8_t *)ptr;
    return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

typedef struct IsoFile
{
    int directory;
    off_t offset;
    off_t size;
} IsoFile;

static IsoFile *iso_find_path(int fd, IsoFile *dir, const char *pathname)
{
    struct iso_directory_record record;
    ssize_t len;
    char name[257];
    uint8_t name_len;
    off_t offset = dir->offset;

    if (!dir->directory) {
        return NULL;
    }

    len = pread(fd, &record, sizeof(record), offset);
    assert(len == sizeof(record));

    while ((offset - dir->offset) < dir->size) {
        size_t size;
        int directory;

        name_len = iso_u8(record.name_len);
        directory = !!(iso_u8(record.flags) & 0x02);

        size = len;
        len = pread(fd, name, name_len, offset + len);
        assert(len == name_len);
        name[name_len] = 0;

        size += len;

        if (size < iso_u8(record.length)) {
            size_t record_length = iso_u8(record.length) - size;
            char buffer[256];
            int i = 0;

            len = pread(fd, buffer, record_length, offset + size);
            assert(len == record_length);
            if (buffer[i] == 0) {
                i++;
            }

            if (record_length > 5 && buffer[i] == 'R' && buffer[i + 1] == 'R') {
                i += 5;
                while (i < record_length) {
                    if (buffer[i] == 'N' && buffer[i + 1] == 'M') {
                        name_len = buffer[i + 2] - 5;
                        memcpy(name, &buffer[i + 5], name_len);
                        name[name_len] = 0;
                        i += buffer[i + 2];
                    } else {
                        break;
                    }
                }
            }
        }

        if (strcmp(name, pathname) == 0) {
            IsoFile *ret = malloc(sizeof(*ret));
            ret->directory = directory;
            ret->offset = iso_u32(record.extent) * 2048;
            ret->size = iso_u32(record.size);
            return ret;
        }

        offset += iso_u8(record.length);

        do {
            len = pread(fd, &record, sizeof(record), offset);
            assert(len == sizeof(record));

            if (iso_u8(record.length) == 0) {
                offset += 2047;
                offset &= ~2047ULL;
            }
        } while (iso_u8(record.length) == 0 && (offset - dir->offset) < dir->size);
    }
    
    return NULL;
}

static IsoFile *iso_find_root(int fd)
{
    off_t offset = 0;
    ssize_t len;
    struct iso_volume_descriptor vd;

    offset = 32768;
    do {
        len = pread(fd, &vd, sizeof(vd), offset);
        assert(len == sizeof(vd));

        if (iso_u8(vd.type) == 1) {
            struct iso_primary_descriptor *pd;
            struct iso_directory_record *root;
            char name[256];
            IsoFile *ret;

            pd = (struct iso_primary_descriptor *)&vd;
            root = (struct iso_directory_record *)pd->root_directory_record;

            memcpy(name, root->name, iso_u8(root->name_len));
            name[iso_u8(root->name_len)] = 0;

            ret = malloc(sizeof(*ret));
            ret->directory = 1;
            ret->offset = iso_u32(root->extent) * 2048;
            ret->size = iso_u32(root->size);
            return ret;
        }

        offset += len;
    } while (iso_u8(vd.type) != 255);

    return NULL;
}

static IsoFile *iso_find_file(int fd, const char *filename)
{
    const char *end;
    IsoFile *cur = iso_find_root(fd);

    while (cur && filename) {
        IsoFile *dir;
        char pathname[257];

        end = strchr(filename, '/');
        if (end) {
            memcpy(pathname, filename, end - filename);
            pathname[end - filename] = 0;
            filename = end + 1;
        } else {
            snprintf(pathname, sizeof(pathname), "%s", filename);
            filename = end;
        }

        dir = iso_find_path(fd, cur, pathname);
        free(cur);
        cur = dir;
    }

    return cur;
}

static void copy_file_from_iso(const char *iso, const char *src, const char *dst)
{
    int iso_fd, dst_fd;
    IsoFile *filp;
    off_t offset;

    iso_fd = open(iso, O_RDONLY);
    dst_fd = open(dst, O_RDWR | O_CREAT | O_TRUNC, 0644);

    g_assert(iso_fd != -1);
    g_assert(dst_fd != -1);

    filp = iso_find_file(iso_fd, src);

    for (offset = 0; offset < filp->size; offset += 2048) {
        char buffer[2048];
        size_t size = MIN(filp->size - offset, 2048);
        ssize_t len;

        len = pread(iso_fd, buffer, size, filp->offset + offset);
        g_assert(len == size);

        len = pwrite(dst_fd, buffer, size, offset);
        g_assert(len == size);
    }

    close(dst_fd);
    close(iso_fd);
}

static const char preseed[] = 
    "d-i	debian-installer/locale	string en_US.UTF-8\n"
    "d-i	debian-installer/splash boolean false\n"
    "d-i	console-setup/ask_detect	boolean false\n"
    "d-i	console-setup/layoutcode	string us\n"
    "d-i	console-setup/variantcode	string \n"
    "d-i	netcfg/get_nameservers	string \n"
    "d-i	netcfg/get_ipaddress	string \n"
    "d-i	netcfg/get_netmask	string 255.255.255.0\n"
    "d-i	netcfg/get_gateway	string \n"
    "d-i	netcfg/confirm_static	boolean true\n"
    "d-i	clock-setup/utc	boolean true\n"
    "d-i 	partman-auto/method string regular\n"
    "d-i 	partman-lvm/device_remove_lvm boolean true\n"
    "d-i 	partman-lvm/confirm boolean true\n"
    "d-i 	partman/confirm_write_new_label boolean true\n"
    "d-i 	partman/choose_partition        select Finish partitioning and write changes to disk\n"
    "d-i 	partman/confirm boolean true\n"
    "d-i 	partman/confirm_nooverwrite boolean true\n"
    "d-i 	partman/default_filesystem string ext3\n"
    "d-i 	clock-setup/utc boolean true\n"
    "d-i	clock-setup/ntp	boolean true\n"
    "d-i	clock-setup/ntp-server	string ntp.ubuntu.com\n"
    "d-i	base-installer/kernel/image	string linux-server\n"
    "d-i	passwd/root-login	boolean false\n"
    "d-i	passwd/make-user	boolean true\n"
    "d-i	passwd/user-fullname	string ubuntu\n"
    "d-i	passwd/username	string ubuntu\n"
    "d-i	passwd/user-password-crypted	password $6$.1eHH0iY$ArGzKX2YeQ3G6U.mlOO3A.NaL22Ewgz8Fi4qqz.Ns7EMKjEJRIW2Pm/TikDptZpuu7I92frytmk5YeL.9fRY4.\n"
    "d-i	passwd/user-uid	string \n"
    "d-i	user-setup/allow-password-weak	boolean false\n"
    "d-i	user-setup/encrypt-home	boolean false\n"
    "d-i	passwd/user-default-groups	string adm cdrom dialout lpadmin plugdev sambashare\n"
    "d-i	apt-setup/services-select	multiselect security\n"
    "d-i	apt-setup/security_host	string security.ubuntu.com\n"
    "d-i	apt-setup/security_path	string /ubuntu\n"
    "d-i	debian-installer/allow_unauthenticated	string false\n"
    "d-i	pkgsel/upgrade	select safe-upgrade\n"
    "d-i	pkgsel/language-packs	multiselect \n"
    "d-i	pkgsel/update-policy	select none\n"
    "d-i	pkgsel/updatedb	boolean true\n"
    "d-i	grub-installer/skip	boolean false\n"
    "d-i	lilo-installer/skip	boolean false\n"
    "d-i	grub-installer/only_debian	boolean true\n"
    "d-i	grub-installer/with_other_os	boolean true\n"
    "d-i	finish-install/keep-consoles	boolean false\n"
    "d-i	finish-install/reboot_in_progress	note \n"
    "d-i	cdrom-detect/eject	boolean true\n"
    "d-i	debian-installer/exit/halt	boolean false\n"
    "d-i	debian-installer/exit/poweroff	boolean true\n"
    "d-i	preseed/late_command		string echo -e '\x1\x2\x3\x4\x5\x6\x7\x8' > /dev/ttyS0\n"
    ;

static const char ks[] =
    "install\n"
    "text\n"
    "reboot\n"
    "lang en_US.UTF-8\n"
    "keyboard us\n"
    "network --bootproto dhcp\n"
    "rootpw 123456\n"
    "firewall --enabled --ssh\n"
    "selinux --enforcing\n"
    "timezone --utc America/New_York\n"
    "firstboot --disable\n"
    "bootloader --location=mbr --append=\"console=tty0 console=ttyS0,115200\"\n"
    "zerombr\n"
    "clearpart --all --initlabel\n"
    "autopart\n"
    "poweroff\n"
    "\n"
    "%packages\n"
    "@base\n"
    "@core\n"
    "%end\n"
    "%post\n"
    "echo -e '\x1\x2\x3\x4\x5\x6\x7\x8'\n"
    "%end\n"
    ;

static int systemf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static int systemf(const char *fmt, ...)
{
    char buffer[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    g_test_message("Running command %s\n", buffer);

    return system(buffer);
}

static bool check_executable(const char *name)
{
    return (access(name, X_OK) == 0);
}

typedef struct WeightedChoice
{
    const char *string;
    int percentage;
} WeightedChoice;

static const char *choose(WeightedChoice *choices)
{
    int i, value;
    int cur_percentage = 0;

    value = g_test_rand_int_range(0, 100);
    for (i = 0; choices[i].string; i++) {
        cur_percentage += choices[i].percentage;
        if (value < cur_percentage) {
            return choices[i].string;
        }
    }

    g_assert_not_reached();
    return NULL;
}

static void add_choice(WeightedChoice *choices, const char *item)
{
    int count, i;
    int percentage;

    for (count = 0; choices[count].string; count++) {
    }

    choices[count].string = item;
    count++;

    percentage = 100 / count;

    for (i = 0; i < count; i++) {
        choices[i].percentage = percentage;
    }

    choices[0].percentage += (100 - ((100 / count) * count));
}

static void favor_choice(WeightedChoice *choices, const char *item)
{
    /* FIXME */
}

static void test_image(const char *command, const char *iso,
                       const char *kernel, const char *initrd,
                       const char *cmdline, const char *config_file,
                       WeightedChoice *nic_types,
                       WeightedChoice *blk_types,
                       WeightedChoice *cache_types,
                       WeightedChoice *disk_formats,
                       WeightedChoice *aio_methods,
                       WeightedChoice *vga_types,
                       WeightedChoice *machine_types,
                       bool enable_smp, const char *accel_type)
{
    char buffer[1024];
    int fd;
    pid_t pid;
    int status, ret;
    long max_cpus, max_mem;
    int num_cpus, mem_mb;
    struct sockaddr_un addr;
    const char *tmp_image = "/tmp/test-linux.img";
    const char *tmp_kernel = "/tmp/test-vmlinuz";
    const char *tmp_initrd = "/tmp/test-initrd";
    const char *tmp_sock = "/tmp/test-linux.sock";
    const char *nic_type, *blk_type, *cache_type, *disk_format, *aio_method;
    const char *vga_type, *machine_type;
    int connect_count;

    max_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_cpus = MIN(g_test_rand_int_range(1, max_cpus + 1), 8);

    if (!enable_smp) {
        num_cpus = 1;
    }

    /* Don't use more than 1/2 of physical memory */
    max_mem = (sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE)) >> 20;
    max_mem /= 2;

    mem_mb = (g_test_rand_int_range(128, max_mem) + 7) & ~0x03;

    nic_type = choose(nic_types);
    blk_type = choose(blk_types);
    cache_type = choose(cache_types);
    disk_format = choose(disk_formats);
    aio_method = choose(aio_methods);
    vga_type = choose(vga_types);
    machine_type = choose(machine_types);

    if (strcmp(cache_type, "none") != 0) {
        aio_method = "threads";
    }

    g_test_message("Using %d VCPUS", num_cpus);
    g_test_message("Using %d MB of RAM", mem_mb);
    g_test_message("Using `%s' network card", nic_type);
    g_test_message("Using `%s' block device, cache=`%s', format=`%s', aio=`%s'",
                   blk_type, cache_type, disk_format, aio_method);
    g_test_message("Using `%s' graphics card", vga_type);
    g_test_message("Using `%s' machine'", machine_type);

    copy_file_from_iso(iso, kernel, tmp_kernel);
    copy_file_from_iso(iso, initrd, tmp_initrd);

    pid = fork();
    if (pid == 0) {
        int status;

        status = systemf("./qemu-img create -f %s %s 10G", disk_format,
                         tmp_image);
        if (status != 0) {
            exit(WEXITSTATUS(status));
        }

        /* FIXME
         *
         * Probably should move to a fork/exec model.  That way we can redirect
         * stdout to a pipe and setup a simple filter on the pipe.  We should
         * have a canary value that if written to stdout, indicates a success
         * test execution.
         *
         * This would allow for "image" tests to be used whereas the image was
         * preconfigured to launch, run some sort of workload, and then upon
         * test completion, write the canary value to stdout.
         */
        status = systemf("%s "
                         "-drive file=%s,if=%s,cache=%s,aio=%s -cdrom %s "
                         "-chardev socket,server=on,wait=on,id=httpd,path=%s "
                         "-net user,guestfwd=tcp:10.0.2.1:80-chardev:httpd "
                         "-net nic,model=%s "
                         "-kernel %s -initrd %s "
                         "-append '%s' -vga %s "
                         "-serial stdio -vnc none -smp %d -m %d "
                         "-machine %s,accel=%s",
                         command, tmp_image, blk_type, cache_type, aio_method,
                         iso, tmp_sock, nic_type, tmp_kernel, tmp_initrd,
                         cmdline, vga_type, num_cpus, mem_mb, machine_type,
                         accel_type);
        unlink(tmp_image);

        if (!WIFEXITED(status)) {
            exit(1);
        }

        exit(WEXITSTATUS(status));
    }

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert(fd != -1);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", tmp_sock);

    connect_count = 0;
    do {
        ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1) {
            usleep(100000);
        }
        connect_count++;
    } while (ret == -1 && connect_count < 100);

    g_assert(connect_count < 100);

    ret = read(fd, buffer, 1);
    g_assert(ret == 1);
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.0 200 OK\r\n"
             "Server: BaseHTTP/0.3 Python/2.6.5\r\n"
             "Date: Wed, 30 Mar 2011 19:46:35 GMT\r\n"
             "Content-type: text/plain\r\n"
             "Content-length: %ld\r\n"
             "\r\n", strlen(config_file));
    ret = write(fd, buffer, strlen(buffer));
    g_assert_cmpint(ret, ==, strlen(buffer));

    ret = write(fd, config_file, strlen(config_file));
    g_assert_cmpint(ret, ==, strlen(config_file));

    ret = waitpid(pid, &status, 0);
    g_assert(ret == pid);
    g_assert(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 3);

    close(fd);
}

static WeightedChoice std_cache_types[] = {
    { "none", 50 },
    { "writethrough", 50 },
    { }
};

static WeightedChoice std_disk_formats[] = {
    { "raw", 50 },
    { "qcow2", 25 },
    { "qed", 25 },
    { }
};

static WeightedChoice std_aio_methods[] = {
    { "threads", 75 },
    { "native", 25 },
    { }
};

static void test_debian_ppc(const char *distro, const char *accel_type)
{
    char iso[1024];
    WeightedChoice nic_types[] = {
        { "ibmveth", 100 },
        { }
    };
    WeightedChoice blk_types[] = {
        { "scsi", 100 },
        { }
    };
    WeightedChoice vga_types[] = {
        { "none", 100 },
        { }
    };
    WeightedChoice machine_types[] = {
        { "pseries", 100 },
        { }
    };

    snprintf(iso, sizeof(iso), "%s/isos/debian-%s-DVD-1.iso",
             getenv("HOME"), distro);

    test_image("ppc64-softmmu/qemu-system-ppc64 ",
               iso, "/install/powerpc64/vmlinux",
               "/install/powerpc64/initrd.gz",
               "priority=critical locale=en_US "
               "url=http://10.0.2.1/server.cfg console=hvc0",
               preseed, nic_types, blk_types, std_cache_types,
               std_disk_formats, std_aio_methods, vga_types,
               machine_types, true, accel_type);
}

static void test_pc_image(const char *command, const char *iso,
                          const char *kernel, const char *initrd,
                          const char *cmdline, const char *config_file,
                          const char *accel_type)
{
    WeightedChoice nic_types[] = {
        { "e1000", 25 },
        { "virtio", 50 },
        { "rtl8139", 25 },
        { }
    };
    WeightedChoice blk_types[] = {
        { "virtio", 40 },
        { "ide", 40 },
        { "scsi", 20 },
        { }
    };
    WeightedChoice vga_types[] = {
        { "cirrus", 80 },
        { "std", 20 },
        { }
    };
    WeightedChoice machine_types[] = {
        { "pc", 80 },
        { "pc-0.14", 10 },
        { "pc-0.13", 5 },
        { "pc-0.12", 5 },
        { }
    };

    test_image(command, iso, kernel, initrd, cmdline, config_file,
               nic_types, blk_types, std_cache_types, std_disk_formats,
               std_aio_methods, vga_types, machine_types, true, accel_type);
}

static void test_ubuntu(const char *distro, const char *accel_type)
{
    char iso[1024];

    snprintf(iso, sizeof(iso), "%s/isos/ubuntu-%s.iso", getenv("HOME"), distro);

    test_pc_image("x86_64-softmmu/qemu-system-x86_64 -enable-kvm",
                  iso, "/install/vmlinuz", "/install/initrd.gz",
                  "priority=critical locale=en_US "
                  "url=http://10.0.2.1/server.cfg console=ttyS0",
                  preseed, accel_type);
}

static void test_fedora(const char *distro, const char *accel_type)
{
    char iso[1024];

    snprintf(iso, sizeof(iso), "%s/isos/Fedora-%s-DVD.iso", getenv("HOME"),
             distro);

    test_pc_image("x86_64-softmmu/qemu-system-x86_64 -enable-kvm",
                  iso, "/isolinux/vmlinuz", "/isolinux/initrd.img",
                  "stage2=hd:LABEL=\"Fedora\" "
                  "ks=http://10.0.2.1/server.ks console=ttyS0",
                  ks, accel_type);
}

static void test_x86_64_linux(const char *accel_type)
{
    WeightedChoice distro_types[] = {
        { "fedora", 25 },
        { "ubuntu", 75 },
        { }
    };
    const char *distro_type;

    distro_type = choose(distro_types);

    g_test_message("Using distro `%s'", distro_type);

    if (strcmp(distro_type, "fedora") == 0) {
        test_fedora("14-x86_64", accel_type);
    } else if (strcmp(distro_type, "ubuntu") == 0) {
        WeightedChoice version_types[] = {
            { "9.10-server-amd64", 30 },
            { "10.04.2-server-amd64", 30 },
            { "10.10-server-amd64", 40 },
            { }
        };
        const char *version_type;

        version_type = choose(version_types);
        g_test_message("Using Ubuntu version `%s'", version_type);
        test_ubuntu(version_type, accel_type);
    }
}

static void test_linux(void)
{
    WeightedChoice accel_types[] = {
        { "kvm", 75 },
        { "tcg", 25 },
        { }
    };
    WeightedChoice target_types[20] = {
        { }
    };
    const char *accel_type;
    const char *target_type;

    if (check_executable("x86_64-softmmu/qemu-system-x86_64")) {
        add_choice(target_types, "x86_64");
    } else {
        g_test_message("Excluding target `x86_64'");
    }

    if (check_executable("ppc64-softmmu/qemu-system-ppc64")) {
        add_choice(target_types, "ppc64");
    } else {
        g_test_message("Excluding target `ppc64'");
    }

    favor_choice(target_types, "x86_64");

    target_type = choose(target_types);

    if (!g_test_slow()) {
        target_type = "x86_64";
        accel_type = "kvm";
    } else if (strcmp(target_type, "x86_64") == 0) {
        accel_type = choose(accel_types);
    } else {
        accel_type = "tcg";
    }

    g_test_message("Using target `%s'", target_type);
    g_test_message("Using accel type `%s'", accel_type);

    if (strcmp(target_type, "x86_64") == 0) {
        test_x86_64_linux(accel_type);
    } else if (strcmp(target_type, "ppc64") == 0) {
        test_debian_ppc("6.0.1a-powerpc", accel_type);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/install/linux", test_linux);

    g_test_run();

    return 0;
}
