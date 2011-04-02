/*
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>

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
    "d-i	debian-installer/exit/poweroff	boolean false\n"
    "d-i	preseed/late_command		string echo -ne '\x1' | dd bs=1 count=1 seek=1281 of=/dev/port\n"
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
    "reboot\n"
    "\n"
    "%packages\n"
    "@base\n"
    "@core\n"
    "%end\n"
    "%post\n"
    "echo -ne '\x1' | dd bs=1 count=1 seek=1281 of=/dev/port\n"
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

    return system(buffer);
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

static void test_image(const char *image, const char *iso,
                       const char *kernel, const char *initrd,
                       const char *cmdline, const char *config_file)
{
    char buffer[1024];
    int fds[2];
    pid_t pid;
    int status, ret;
    long max_cpus, max_mem;
    int num_cpus, mem_mb;
    const char *nic_type, *blk_type, *cache_type, *disk_format, *aio_method;
    const char *vga_type;
    WeightedChoice nic_types[] = {
        { "e1000", 25 },
        { "virtio", 50 },
        { "rtl8139", 25 },
        { }
    };
    WeightedChoice blk_types[] = {
        { "virtio", 50 },
        { "ide", 50 },
        { }
    };
    WeightedChoice cache_types[] = {
        { "none", 50 },
        { "writethrough", 50 },
        { }
    };
    WeightedChoice disk_formats[] = {
        { "raw", 50 },
        { "qcow2", 25 },
        { "qed", 25 },
        { }
    };
    WeightedChoice aio_methods[] = {
        { "threads", 75 },
        { "native", 25 },
        { }
    };
    WeightedChoice vga_types[] = {
        { "cirrus", 80 },
        { "std", 20 },
        { }
    };

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
    g_assert(ret != -1);

    max_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_cpus = MIN(g_test_rand_int_range(1, max_cpus + 1), 8);

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

    if (strcmp(cache_type, "none") != 0) {
        aio_method = "threads";
    }

    g_test_message("Using %d VCPUS", num_cpus);
    g_test_message("Using %d MB of RAM", mem_mb);
    g_test_message("Using `%s' network card", nic_type);
    g_test_message("Using `%s' block device, cache=`%s', format=`%s', aio=`%s'",
                   blk_type, cache_type, disk_format, aio_method);
    g_test_message("Using `%s' graphics card", vga_type);

    pid = fork();
    if (pid == 0) {
        int status;

        status = systemf("./qemu-img create -f %s %s 10G", disk_format, image);
        if (status != 0) {
            exit(WEXITSTATUS(status));
        }

        status = systemf("x86_64-softmmu/qemu-system-x86_64 "
                         "-drive file=%s,if=%s,cache=%s,aio=%s -cdrom %s "
                         "-chardev fdname,fdin=%d,fdout=%d,id=httpd "
                         "-net user,guestfwd=tcp:10.0.2.1:80-chardev:httpd "
                         "-net nic,model=%s -enable-kvm "
                         "-kernel cdrom://%s -initrd cdrom://%s "
                         "-append '%s' -vga %s "
                         "-serial stdio -vnc none -smp %d -m %d ",
                         image, blk_type, cache_type, aio_method,
                         iso, fds[1], fds[1], nic_type, kernel, initrd,
                         cmdline, vga_type, num_cpus, mem_mb);
        unlink(image);

        if (!WIFEXITED(status)) {
            exit(1);
        }

        exit(WEXITSTATUS(status));
    }

    ret = read(fds[0], buffer, 1);
    g_assert(ret == 1);
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.0 200 OK\r\n"
             "Server: BaseHTTP/0.3 Python/2.6.5\r\n"
             "Date: Wed, 30 Mar 2011 19:46:35 GMT\r\n"
             "Content-type: text/plain\r\n"
             "Content-length: %ld\r\n"
             "\r\n", strlen(config_file));
    ret = write(fds[0], buffer, strlen(buffer));
    g_assert_cmpint(ret, ==, strlen(buffer));

    ret = write(fds[0], config_file, strlen(config_file));
    g_assert_cmpint(ret, ==, strlen(config_file));

    ret = waitpid(pid, &status, 0);
    g_assert(ret == pid);
    g_assert(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 6);
}

static void test_ubuntu(gconstpointer data)
{
    const char *distro = data;
    char image[1024];
    char iso[1024];

    snprintf(image, sizeof(image), "/tmp/ubuntu-%s.img", distro);
    snprintf(iso, sizeof(iso), "~/isos/ubuntu-%s.iso", distro);

    test_image(image, iso, "/install/vmlinuz", "/install/initrd.gz",
               "priority=critical locale=en_US "
               "url=http://10.0.2.1/server.cfg console=ttyS0",
               preseed);
}

static void test_ppc_image(const char *image, const char *iso,
                           const char *kernel, const char *initrd,
                           const char *cmdline, const char *config_file)
{
    char buffer[1024];
    int fds[2];
    pid_t pid;
    int status, ret;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
    g_assert(ret != -1);

    pid = fork();
    if (pid == 0) {
        int status;

        status = systemf("./qemu-img create -f raw %s 10G", image);
        if (status != 0) {
            exit(WEXITSTATUS(status));
        }

        status = systemf("ppc64-softmmu/qemu-system-ppc64 "
                         "-drive file=%s,if=scsi -cdrom %s "
                         "-chardev fdname,fdin=%d,fdout=%d,id=httpd "
                         "-net user,guestfwd=tcp:10.0.2.1:80-chardev:httpd "
                         "-net nic -enable-kvm "
                         "-kernel cdrom://%s -initrd cdrom://%s "
                         "-append '%s' "
                         "-serial stdio -vnc none -m 1G -M pseries ",
                         image, iso, fds[1], fds[1],
                         kernel, initrd, cmdline);
        unlink(image);

        if (!WIFEXITED(status)) {
            exit(1);
        }

        exit(WEXITSTATUS(status));
    }

    ret = read(fds[0], buffer, 1);
    g_assert(ret == 1);
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.0 200 OK\r\n"
             "Server: BaseHTTP/0.3 Python/2.6.5\r\n"
             "Date: Wed, 30 Mar 2011 19:46:35 GMT\r\n"
             "Content-type: text/plain\r\n"
             "Content-length: %ld\r\n"
             "\r\n", strlen(config_file));
    ret = write(fds[0], buffer, strlen(buffer));
    g_assert_cmpint(ret, ==, strlen(buffer));

    ret = write(fds[0], config_file, strlen(config_file));
    g_assert_cmpint(ret, ==, strlen(config_file));

    ret = waitpid(pid, &status, 0);
    g_assert(ret == pid);
    g_assert(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 6);
}

static void test_fedora(gconstpointer data)
{
    const char *distro = data;
    char image[1024];
    char iso[1024];

    snprintf(image, sizeof(image), "/tmp/fedora-%s.img", distro);
    snprintf(iso, sizeof(iso), "~/isos/Fedora-%s-DVD.iso", distro);

    test_image(image, iso, "/isolinux/vmlinuz", "/isolinux/initrd.img",
               "stage2=hd:LABEL=\"Fedora\" "
               "ks=http://10.0.2.1/server.ks console=ttyS0",
               ks);
}

static void test_ppc_debian(gconstpointer data)
{
    const char *distro = data;
    char image[1024];
    char iso[1024];

    snprintf(image, sizeof(image), "/tmp/debian-%s.img", distro);
    snprintf(iso, sizeof(iso), "~/isos/debian-%s-DVD-1.iso", distro);

    test_ppc_image(image, iso, "/install/powerpc64/vmlinux",
                   "/install/powerpc64/initrd.gz",
                   "priority=critical locale=en_US "
                   "url=http://10.0.2.1/server.cfg console=ttyS0",
                   preseed);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/debian/6.0.1a/ppc64", "6.0.1a-powerpc",
                         test_ppc_debian);
    g_test_add_data_func("/fedora/13/i386", "13-i386", test_fedora);
    g_test_add_data_func("/fedora/14/x86_64", "14-x86_64", test_fedora);
    g_test_add_data_func("/ubuntu/9.10/server/amd64", "9.10-server-amd64", test_ubuntu);
    g_test_add_data_func("/ubuntu/10.04.2/server/amd64", "10.04.2-server-amd64", test_ubuntu);
    g_test_add_data_func("/ubuntu/10.10/server/amd64", "10.10-server-amd64", test_ubuntu);

    g_test_run();

    return 0;
}
