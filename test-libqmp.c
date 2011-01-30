#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "libqmp.h"

/**
 * TODO:
 *  1) make a QmpSession that actually works
 *  2) split the type, alloc, and free functions into a separate header/file
 *     such that we don't have to expose qobject in the library interface
 *  3) figure out how to support errors without exposing qobject
 */

int main(int argc, char **argv)
{
    QmpSession *sess = NULL;
    VersionInfo *info;
    struct sockaddr_un addr;
    int s;

    if (argc != 2) {
        return 1;
    }

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        return 1;
    }

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", argv[1]);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        return 1;
    }

    sess = qmp_session_new(s);

    info = libqmp_query_version(sess, NULL);
    printf("%ld.%ld.%ld\n", info->qemu.major, info->qemu.minor, info->qemu.micro);
    qmp_free_VersionInfo(info);

    libqmp_quit(sess, NULL);

    return 0;
}
