/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"
#include "qerror.h"
#include "monitor.h"

static void GCC_FMT_ATTR(2, 3) qerror_abort(const QError *qerr,
                                            const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "qerror: bad call in function '%s':\n", qerr->func);
    fprintf(stderr, "qerror: -> ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\nqerror: call at %s:%d\n", qerr->file, qerr->linenr);
    abort();
}

static void GCC_FMT_ATTR(2, 0) qerror_set_data(QError *qerr,
                                               const char *fmt, va_list *va)
{
    QObject *obj;

    obj = qobject_from_jsonv(fmt, va);
    if (!obj) {
        qerror_abort(qerr, "invalid format '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "error format is not a QDict '%s'", fmt);
    }

    qerr->error = qobject_to_qdict(obj);

    obj = qdict_get(qerr->error, "class");
    if (!obj) {
        qerror_abort(qerr, "missing 'class' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QSTRING) {
        qerror_abort(qerr, "'class' key value should be a QString");
    }
    
    obj = qdict_get(qerr->error, "data");
    if (!obj) {
        qerror_abort(qerr, "missing 'data' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "'data' key value should be a QDICT");
    }
}

/**
 * qerror_from_info(): Create a new QError from error information
 *
 * The information consists of:
 *
 * - file   the file name of where the error occurred
 * - linenr the line number of where the error occurred
 * - func   the function name of where the error occurred
 * - fmt    JSON printf-like dictionary, there must exist keys 'class' and
 *          'data'
 * - va     va_list of all arguments specified by fmt
 *
 * Return strong reference.
 */
QError *qerror_from_info(const char *file, int linenr, const char *func,
                         const char *fmt, va_list *va)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);
    qerr->linenr = linenr;
    qerr->file = file;
    qerr->func = func;

    if (!fmt) {
        qerror_abort(qerr, "QDict not specified");
    }

    qerror_set_data(qerr, fmt, va);
    qerror_set_desc(qerr, fmt);

    return qerr;
}

/**
 * qerror_print(): Print QError data
 *
 * This function will print the member 'desc' of the specified QError object,
 * it uses error_report() for this, so that the output is routed to the right
 * place (ie. stderr or Monitor's device).
 */
void qerror_print(QError *qerror)
{
    QString *qstring = qerror_human(qerror);
    loc_push_restore(&qerror->loc);
    error_report("%s", qstring_get_str(qstring));
    loc_pop(&qerror->loc);
    QDECREF(qstring);
}

void qerror_report_internal(const char *file, int linenr, const char *func,
                            const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    va_start(va, fmt);
    qerror = qerror_from_info(file, linenr, func, fmt, &va);
    va_end(va);

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerror);
    } else {
        qerror_print(qerror);
        QDECREF(qerror);
    }
}

void qerror_report_err(Error *err)
{
    qerror_report(QERR_UNDEFINED_ERROR);
}

