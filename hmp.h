/*
 * Human Monitor Interface
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HMP_H
#define HMP_H

#include "qemu-common.h"
#include "qapi-types.h"

void hmp_info_name(Monitor *mon);
void hmp_eject(Monitor *mon, const QDict *args);
void hmp_block_passwd(Monitor *mon, const QDict *qdict);

#endif
