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
