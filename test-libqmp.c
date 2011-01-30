#include <stdio.h>
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
    Error *err = NULL;

    info = libqmp_query_version(sess, &err);
    printf("%ld.%ld.%ld\n", info->qemu.major, info->qemu.minor, info->qemu.micro);
    qmp_free_VersionInfo(info);

    return 0;
}
