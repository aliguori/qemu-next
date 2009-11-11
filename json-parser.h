#ifndef QEMU_JSON_PARSER_H
#define QEMU_JSON_PARSER_H

#include "qemu-common.h"
#include "qlist.h"

QObject *json_parser_parse(QList *tokens, va_list *ap);

#endif
