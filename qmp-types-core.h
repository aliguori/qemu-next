/*
 * QAPI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#ifndef QMP_TYPES_CORE_H
#define QMP_TYPES_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "error.h"

typedef struct QmpSignal QmpSignal;
typedef struct QmpCommandState QmpCommandState;
typedef struct QmpState QmpState;

#define BUILD_ASSERT(cond) do {     \
    (void)sizeof(int[-1+!!(cond)]); \
} while (0)

#define BUILD_BUG() BUILD_ASSERT(0)

#endif
