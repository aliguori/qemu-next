#ifndef HMP_CORE_H
#define HMP_CORE_H

#include "monitor.h"

typedef void (HmpCommandFunc)(Monitor *, int argc, char **argv);

void hmp_register_command(const char *name, HmpCommandFunc *fn);
void hmp_register_info_command(const char *name, HmpCommandFunc *fn);

#endif

