#include "qemu-common.h"
#include "monitor.h"

SpiceInfo *qmp_query_spice(Error **errp)
{
    SpiceInfo *info;

    info = qmp_alloc_spice_info();
    info->enabled = false;

    return info;
}