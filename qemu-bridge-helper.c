/*
 * QEMU Bridge Helper
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "config-host.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

#include <net/if.h>

#include <linux/sockios.h>

#include "net/tap-linux.h"

static int has_vnet_hdr(int fd)
{
    unsigned int features;
    struct ifreq ifreq;

    if (ioctl(fd, TUNGETFEATURES, &features) == -1) {
        return -errno;
    }

    if (!(features & IFF_VNET_HDR)) {
        return -ENOTSUP;
    }

    if (ioctl(fd, TUNGETIFF, &ifreq) != -1 || errno != EBADFD) {
        return -ENOTSUP;
    }

    return 1;
}

static void prep_ifreq(struct ifreq *ifr, const char *ifname)
{
    memset(ifr, 0, sizeof(*ifr));
    snprintf(ifr->ifr_name, IFNAMSIZ, "%s", ifname);
}

static int send_fd(int c, int fd)
{
    char msgbuf[CMSG_SPACE(sizeof(fd))];
    struct msghdr msg = {
        .msg_control = msgbuf,
        .msg_controllen = sizeof(msgbuf),
    };
    struct cmsghdr *cmsg;
    struct iovec iov;
    char req[1] = { 0x00 };

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
    msg.msg_controllen = cmsg->cmsg_len;

    iov.iov_base = req;
    iov.iov_len = sizeof(req);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    return sendmsg(c, &msg, 0);
}

int main(int argc, char **argv)
{
    struct ifreq ifr;
    int fd, ctlfd, unixfd;
    int use_vnet = 0;
    int mtu;
    const char *bridge;
    char iface[IFNAMSIZ];
    int index;

    /* parse arguments */
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s [--use-vnet] BRIDGE FD\n", argv[0]);
        return 1;
    }

    index = 1;
    if (strcmp(argv[index], "--use-vnet") == 0) {
        use_vnet = 1;
        index++;
        if (argc == 3) {
            fprintf(stderr, "invalid number of arguments\n");
            return -1;
        }
    }

    bridge = argv[index++];
    unixfd = atoi(argv[index++]);

    /* open a socket to use to control the network interfaces */
    ctlfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctlfd == -1) {
        fprintf(stderr, "failed to open control socket\n");
        return -errno;
    }

    /* open the tap device */
    fd = open("/dev/net/tun", O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "failed to open /dev/net/tun\n");
        return -errno;
    }

    /* request a tap device, disable PI, and add vnet header support if
     * requested and it's available. */
    prep_ifreq(&ifr, "tap%d");
    ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
    if (use_vnet && has_vnet_hdr(fd)) {
        ifr.ifr_flags |= IFF_VNET_HDR;
    }

    if (ioctl(fd, TUNSETIFF, &ifr) == -1) {
        fprintf(stderr, "failed to create tun device\n");
        return -errno;
    }

    /* save tap device name */
    snprintf(iface, sizeof(iface), "%s", ifr.ifr_name);

    /* get the mtu of the bridge */
    prep_ifreq(&ifr, bridge);
    if (ioctl(ctlfd, SIOCGIFMTU, &ifr) == -1) {
        fprintf(stderr, "failed to get mtu of bridge `%s'\n", bridge);
        return -errno;
    }

    /* save mtu */
    mtu = ifr.ifr_mtu;

    /* set the mtu of the interface based on the bridge */
    prep_ifreq(&ifr, iface);
    ifr.ifr_mtu = mtu;
    if (ioctl(ctlfd, SIOCSIFMTU, &ifr) == -1) {
        fprintf(stderr, "failed to set mtu of device `%s' to %d\n",
                iface, mtu);
        return -errno;
    }

    /* add the interface to the bridge */
    prep_ifreq(&ifr, bridge);
    ifr.ifr_ifindex = if_nametoindex(iface);

    if (ioctl(ctlfd, SIOCBRADDIF, &ifr) == -1) {
        fprintf(stderr, "failed to add interface `%s' to bridge `%s'\n",
                iface, bridge);
        return -errno;
    }

    /* bring the interface up */
    prep_ifreq(&ifr, iface);
    if (ioctl(ctlfd, SIOCGIFFLAGS, &ifr) == -1) {
        fprintf(stderr, "failed to get interface flags for `%s'\n", iface);
        return -errno;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(ctlfd, SIOCSIFFLAGS, &ifr) == -1) {
        fprintf(stderr, "failed to set bring up interface `%s'\n", iface);
        return -errno;
    }

    /* write fd to the domain socket */
    if (send_fd(unixfd, fd) == -1) {
        fprintf(stderr, "failed to write fd to unix socket\n");
        return -errno;
    }

    /* ... */

    /* profit! */

    close(fd);

    close(ctlfd);

    return 0;
}
