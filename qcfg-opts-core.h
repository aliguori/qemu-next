#ifndef QCFG_OPTS_CORE_H
#define QCFG_OPTS_CORE_H

#include "qemu-common.h"
#include "qmp-types.h"
#include "qcfg-marshal.h"

typedef void (QcfgHandlerArg)(const char *, Error **);
typedef void (QcfgHandlerNoarg)(Error **);

void qcfg_options_init(void);
void qcfg_register_option_arg(const char *name, QcfgHandlerArg *fn);
void qcfg_register_option_noarg(const char *name, QcfgHandlerNoarg *fn);

#endif

