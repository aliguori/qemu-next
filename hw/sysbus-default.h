#ifndef QEMU_SYSBUS_DEFAULT_H
#define QEMU_SYSBUS_DEFAULT_H

#include "qdev.h"

/* A singleton access function used to access the main System bus.  Use of this
 * function is highly discouraged.  A BusState * should always be propagated
 * and used in new code.  However, this is needed until all of the existing
 * code is improved.
 */
BusState *sysbus_get_default(void);

#endif
