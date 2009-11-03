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
#include <getopt.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

#include <net/if.h>

#include <linux/sockios.h>

#include "net/tap-linux.h"

#define MAX_ACLS (128)
#define DEFAULT_ACL_FILE CONFIG_QEMU_CONFDIR "/bridge.conf"

enum {
    ACL_ALLOW = 0,
    ACL_ALLOW_ALL,
    ACL_DENY,
    ACL_DENY_ALL,
};

typedef struct ACLRule
{
    int type;
    char iface[IFNAMSIZ];
} ACLRule;

static int parse_acl_file(const char *filename, ACLRule *acls, int *pacl_count)
{
    int acl_count = *pacl_count;
    FILE *f;
    char line[4096];

    f = fopen(filename, "r");
    if (f == NULL) {
        return -1;
    }

    while (acl_count != MAX_ACLS &&
           fgets(line, sizeof(line), f) != NULL) {
        char *ptr = line;
        char *cmd, *arg, *argend;

        while (isspace(*ptr)) {
            ptr++;
        }

        /* skip comments and empty lines */
        if (*ptr == '#' || *ptr == 0) {
            continue;
        }

        cmd = ptr;
        arg = strchr(cmd, ' ');
        if (arg == NULL) {
            arg = strchr(cmd, '\t');
        }

        if (arg == NULL) {
            fprintf(stderr, "Invalid config line:\n  %s\n", line);
            fclose(f);
            errno = EINVAL;
            return -1;
        }

        *arg = 0;
        arg++;
        while (isspace(*arg)) {
            arg++;
        }

        argend = arg + strlen(arg);
        while (arg != argend && isspace(*(argend - 1))) {
            argend--;
        }
        *argend = 0;

        if (strcmp(cmd, "deny") == 0) {
            if (strcmp(arg, "all") == 0) {
                acls[acl_count].type = ACL_DENY_ALL;
            } else {
                acls[acl_count].type = ACL_DENY;
                snprintf(acls[acl_count].iface, IFNAMSIZ, "%s", arg);
            }
            acl_count++;
        } else if (strcmp(cmd, "allow") == 0) {
            if (strcmp(arg, "all") == 0) {
                acls[acl_count].type = ACL_ALLOW_ALL;
            } else {
                acls[acl_count].type = ACL_ALLOW;
                snprintf(acls[acl_count].iface, IFNAMSIZ, "%s", arg);
            }
            acl_count++;
        } else if (strcmp(cmd, "include") == 0) {
            /* ignore errors */
            parse_acl_file(arg, acls, &acl_count);
        } else {
            fprintf(stderr, "Unknown command `%s'\n", cmd);
            fclose(f);
            errno = EINVAL;
            return -1;
        }
    }

    *pacl_count = acl_count;

    fclose(f);

    return 0;
}

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
    int fd, ctlfd, unixfd = -1;
    int use_vnet = 0;
    int mtu;
    const char *bridge = NULL;
    char iface[IFNAMSIZ];
    struct option lopts[] = {
        { "use-vnet", 0, 0, 'v' },
        { "bridge", 1, 0, 'b' },
        { "unix-fd", 1, 0, 'u' },
        { },
    };
    int opt_ind, ch;
    ACLRule acls[MAX_ACLS];
    int acl_count = 0;
    int i, access_allowed;

    while ((ch = getopt_long(argc, argv, "", lopts, &opt_ind)) != -1) {
        switch (ch) {
        case 'v':
            use_vnet = 1;
            break;
        case 'b':
            bridge = optarg;
            break;
        case 'u':
            unixfd = atoi(optarg);
            break;
        default:
            break;
        }
    }

    if (unixfd == -1 || bridge == NULL) {
        fprintf(stderr, "Usage: %s [--use-vnet] --bridge=BRIDGE --unix-fd=FD\n", argv[0]);
        return 1;
    }

    /* parse default acl file */
    if (parse_acl_file(DEFAULT_ACL_FILE, acls, &acl_count) == -1) {
        fprintf(stderr, "failed to parse default acl file `%s'\n",
                DEFAULT_ACL_FILE);
        return -errno;
    }

    /* validate bridge against acl -- default policy is to deny */
    access_allowed = 0;
    for (i = 0; i < acl_count; i++) {
        switch (acls[i].type) {
        case ACL_ALLOW_ALL:
            access_allowed = 1;
            break;
        case ACL_ALLOW:
            if (strcmp(bridge, acls[i].iface) == 0) {
                access_allowed = 1;
            }
            break;
        case ACL_DENY_ALL:
            access_allowed = 0;
            break;
        case ACL_DENY:
            if (strcmp(bridge, acls[i].iface) == 0) {
                access_allowed = 0;
            }
            break;
        }
    }

    if (access_allowed == 0) {
        fprintf(stderr, "access denied by acl file\n");
        return -EPERM;
    }

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
        if (errno == EPERM) {
            fprintf(stderr, "do you have write access to /dev/net/tun?\n");
        }
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
