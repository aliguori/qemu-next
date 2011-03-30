/*
 * Rough idea:
 *
 * Create a socket pair
 *
 * pid = fork()
 * Launch a guest with system():
 *
 * x86_64-softmmu/qemu-system-x86_64
     -hda ~/images/ubuntu.img -cdrom ~/isos/ubuntu-10.04.2-server-amd64.iso
 *   -chardev fdname,fdin=10,fdout=10,id=httpd -serial stdio -vnc none
 *   -net user,guestfwd=tcp:10.0.2.1:80-chardev:httpd -net nic -enable-kvm
 *   -kernel cdrom:///install/vmlinuz -initrd cdrom:///install/initrd.gz
 *   -append 'priority=critical locale=en_US url=http://10.0.2.1/server.cfg console=ttyS0'
 *
 * wait for read to come in on the socket
 * write the http response
 *
 * waitpid(pid);
 *
 * take the exit code, check for code & 0x2 for a guest exit, then do code >> 2
 * to determine the guest exit status.
 *
 * look for an guest exit status of 1.
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>

const char rsp[] = 
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

static void test_ubuntu_10_04_2_server_amd64(void)
{
    int status;
    pid_t pid;
    int ret;
    int fds[2];
    char buffer[1024];

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
    g_assert(ret != -1);

    pid = fork();
    if (pid == 0) {
        int status;
        char buffer[1024];

        snprintf(buffer, sizeof(buffer),
                 "x86_64-softmmu/qemu-system-x86_64 "
                 "-hda ~/images/ubuntu.img "
                 "-cdrom ~/isos/ubuntu-10.04.2-server-amd64.iso "
                 "-chardev fdname,fdin=%d,fdout=%d,id=httpd "
                 "-net user,guestfwd=tcp:10.0.2.1:80-chardev:httpd "
                 "-net nic -enable-kvm "
                 "-kernel cdrom:///install/vmlinuz "
                 "-initrd cdrom:///install/initrd.gz "
                 "-append 'priority=critical locale=en_US url=http://10.0.2.1/server.cfg' ",
                 fds[1], fds[1]);

        status = system(buffer);
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
             "\r\n", strlen(rsp));
    ret = write(fds[0], buffer, strlen(buffer));
    g_assert_cmpint(ret, ==, strlen(buffer));

    ret = write(fds[0], rsp, strlen(rsp));
    g_assert_cmpint(ret, ==, strlen(rsp));

    ret = waitpid(pid, &status, 0);
    g_assert(ret == pid);
    g_assert(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 6);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/ubuntu/10.04.2/server/amd64", test_ubuntu_10_04_2_server_amd64);

    g_test_run();

    return 0;
}
