/*
 * QJSON Module
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qstring.h"
#include "qint.h"
#include "qdict.h"
#include "qlist.h"
#include "qfloat.h"
#include "qbool.h"
#include "qjson.h"

typedef struct JSONParserContext
{
    const char *data;
    int lineno;
} JSONParserContext;

#define BUG_ON(cond) assert(!(cond))

/**
 * TODO
 *
 * 3) determine if we need to support null
 * 4) more vardac tests
 */

static QObject *parse_value(JSONParserContext *ctxt, const char *string, size_t *length, va_list *ap);

static void wchar_to_utf8(uint16_t wchar, char *buffer, size_t buffer_length)
{
    if (wchar <= 0x007F) {
        BUG_ON(buffer_length < 2);

        buffer[0] = wchar & 0x7F;
        buffer[1] = 0;
    } else if (wchar <= 0x07FF) {
        BUG_ON(buffer_length < 3);

        buffer[0] = 0xC0 | ((wchar >> 6) & 0x1F);
        buffer[1] = 0x80 | (wchar & 0x3F);
        buffer[2] = 0;
    } else {
        BUG_ON(buffer_length < 4);

        buffer[0] = 0xE0 | ((wchar >> 12) & 0x0F);
        buffer[1] = 0x80 | ((wchar >> 6) & 0x3F);
        buffer[2] = 0x80 | (wchar & 0x3F);
        buffer[3] = 0;
    }
}

static int hex2decimal(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }

    return -1;
}

static void parse_error(JSONParserContext *ctxt, const char *at, const char *msg)
{
    const char *linestart = at;
    const char *lineend = at;
    char linebuf[80];
    size_t len, pos;

    while (linestart != ctxt->data && *(linestart - 1) != '\n') {
        linestart--;
    }

    while (*lineend && *lineend != '\n') {
        lineend++;
    }

    len = MIN(79, lineend - linestart);
    memcpy(linebuf, linestart, len);
    linebuf[len] = 0;

    pos = at - linestart;

    fprintf(stderr, "parse error: %s\n", msg);

    if (pos <= len) {
        fprintf(stderr, " %s\n", linebuf);
        for (pos++; pos; pos--) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "^\n");
    }
    
}

static size_t parse_skip(JSONParserContext *ctxt, const char *data)
{
    const char *ptr = data;

    while (*ptr == ' ' || *ptr == '\r' || *ptr == '\n' ||
           *ptr == '\t') {
        if (*ptr == '\n') {
            ctxt->lineno++;
        }
        ptr++;
    }

    return (ptr - data);
}

/**
 * parse_string(): Parse a json string and return a QObject
 *
 *  string
 *      ""
 *      " chars "
 *  chars
 *      char
 *      char chars
 *  char
 *      any-Unicode-character-
 *          except-"-or-\-or-
 *          control-character
 *      \"
 *      \\
 *      \/
 *      \b
 *      \f
 *      \n
 *      \r
 *      \t
 *      \u four-hex-digits 
 */
static QString *parse_string(JSONParserContext *ctxt,
                             const char *data, size_t *length, va_list *ap)
{
    const char *ptr = data;
    QString *str = NULL;
    int double_quote = 1;

    ptr += parse_skip(ctxt, ptr);

    /* handle string format */
    if (ap) {
        if (*ptr != '%') {
            goto out;
        }
        ptr++;

        if (*ptr != 's') {
            goto out;
        }
        ptr++;

        *length = (ptr - data);
        return qstring_from_str(va_arg(*ap, const char *));
    }

    if (*ptr != '"' && *ptr != '\'') {
        goto out;
    }
    if (*ptr == '"') {
        double_quote = 1;
    } else {
        double_quote = 0;
    }
    ptr++;

    str = qstring_new();
    while (*ptr && 
           ((double_quote && *ptr != '"') || (!double_quote && *ptr != '\''))) {
        if (*ptr == '\\') {
            ptr++;

            switch (*ptr) {
            case '"':
                qstring_append(str, "\"");
                ptr++;
                break;
            case '\'':
                qstring_append(str, "'");
                ptr++;
                break;
            case '\\':
                qstring_append(str, "\\");
                ptr++;
                break;
            case '/':
                qstring_append(str, "/");
                ptr++;
                break;
            case 'b':
                qstring_append(str, "\b");
                ptr++;
                break;
            case 'n':
                qstring_append(str, "\n");
                ptr++;
                break;
            case 'r':
                qstring_append(str, "\r");
                ptr++;
                break;
            case 't':
                qstring_append(str, "\t");
                ptr++;
                break;
            case 'u': {
                uint16_t unicode_char = 0;
                char utf8_char[4];
                int i = 0;

                ptr++;

                for (i = 0; i < 4; i++) {
                    if (qemu_isxdigit(*ptr)) {
                        unicode_char |= hex2decimal(*ptr) << ((3 - i) * 4);
                    } else {
                        parse_error(ctxt, ptr,
                                    "invalid hex escape sequence in string");
                        goto out;
                    }
                    ptr++;
                }

                wchar_to_utf8(unicode_char, utf8_char, sizeof(utf8_char));
                qstring_append(str, utf8_char);
            }   break;
            default:
                parse_error(ctxt, ptr, "invalid escape sequence in string");
                goto out;
            }
        } else {
            char dummy[2];

            dummy[0] = *ptr++;
            dummy[1] = 0;

            qstring_append(str, dummy);
        }
    }

    if ((double_quote && *ptr != '"') || (!double_quote && *ptr != '\'')) {
        parse_error(ctxt, ptr, "unterminated string literal");
        goto out;
    }
    ptr++;

    *length = (ptr - data);

    return str;

out:
    QDECREF(str);
    return NULL;
}

/**
 * parse_number(): Parse a json number and return a QObject
 *
 *  number
 *      int
 *      int frac
 *      int exp
 *      int frac exp 
 *  int
 *      digit
 *      digit1-9 digits
 *      - digit
 *      - digit1-9 digits 
 *  frac
 *      . digits
 *  exp
 *      e digits
 *  digits
 *      digit
 *      digit digits
 *  e
 *      e
 *      e+
 *      e-
 *      E
 *      E+
 *      E-
 */
static QObject *parse_number(JSONParserContext *ctxt,
                             const char *data, size_t *length, va_list *ap)
{
    const char *ptr = data;
    const char *number_start;
    QInt *number = NULL;
    int64_t value;
    int factor = 1;
    int non_integer = 0;

    ptr += parse_skip(ctxt, ptr);

    number_start = ptr;

    /* handle string format */
    if (ap && *ptr == '%') {
        int64_t value;

        ptr++;

        if (*ptr == 'd') {
            /* int */
            ptr++;
            value = va_arg(*ap, int);
        } else if (*ptr == 'l') {
            ptr++;
            if (*ptr == 'd') {
                /* long */
                ptr++;
                value = va_arg(*ap, long);
            } else if (*ptr == 'l') {
                ptr++;
                if (*ptr == 'd') {
                    ptr++;
                    value = va_arg(*ap, long long);
                } else {
                    parse_error(ctxt, ptr, "invalid escape sequence");
                    goto out;
                }
            } else {
                parse_error(ctxt, ptr, "invalid escape sequence");
                goto out;
            }
        } else if (*ptr == 'i') {
            int val;
            ptr++;
            *length = ptr - data;
            val = va_arg(*ap, int);
            return QOBJECT(qbool_from_int(val));
        } else if (*ptr == 'f') {
            double val;
            ptr++;
            *length = (ptr - data);
            val = va_arg(*ap, double);
            return QOBJECT(qfloat_from_double(val));
        } else {
            goto out;
        }

        *length = (ptr - data);
        return QOBJECT(qint_from_int(value));
    }

    if (*ptr == '-') {
        factor = -1;
        ptr++;
    }

    if (*ptr == '0') {
        value = 0;
        ptr++;
    } else if (*ptr >= '1' && *ptr <= '9') {
        value = *ptr - '0';
        ptr++;

        while (*ptr >= '0' && *ptr <= '9') {
            value *= 10;
            value += *ptr - '0';
            ptr++;
        }

        value *= factor;
    } else {
        goto out;
    }

    if (*ptr == '.') {
        ptr++;

        non_integer = 1;

        if (!qemu_isdigit(*ptr)) {
            parse_error(ctxt, ptr, "expecting mantissa");
            goto out;
        }
        ptr++;

        while (qemu_isdigit(*ptr)) {
            ptr++;
        }
    }

    if (*ptr == 'e' || *ptr == 'E') {
        ptr++;

        non_integer = 1;

        if (*ptr == '-' || *ptr == '+') {
            ptr++;
        }

        if (!qemu_isdigit(*ptr)) {
            parse_error(ctxt, ptr, "expecting exponent");
            goto out;
        }
        ptr++;

        while (qemu_isdigit(*ptr)) {
            ptr++;
        }
    }

    *length = ptr - data;

    if (non_integer) {
        char buffer[129];

        if ((ptr - number_start) > 128) {
            parse_error(ctxt, ptr, "floating point too larger");
            goto out;
        }

        memcpy(buffer, number_start, (ptr - number_start));
        buffer[ptr - number_start] = 0;

        /* floats are hard to parse so punt to libc */
        return QOBJECT(qfloat_from_double(strtod(buffer, NULL)));
    } else {
        return QOBJECT(qint_from_int(value));
    }

out:
    QDECREF(number);
    return NULL;
}

static int parse_pair(JSONParserContext *ctxt, QDict *dict, const char *data, size_t *length, va_list *ap)
{
    const char *ptr = data;
    QString *key;
    QObject *value;
    size_t len = 0;

    key = parse_string(ctxt, ptr, &len, ap);
    if (key == NULL) {
        parse_error(ctxt, ptr, "Dict key requires string");
        goto out;
    }
    ptr += len;

    ptr += parse_skip(ctxt, ptr);

    if (*ptr != ':') {
        QDECREF(key);
        parse_error(ctxt, ptr, "Missing separator in dict");
        goto out;
    }
    ptr++;

    value = parse_value(ctxt, ptr, &len, ap);
    if (value == NULL) {
        QDECREF(key);
        parse_error(ctxt, ptr, "Missing value in dict");
        goto out;
    }
    ptr += len;

    qdict_put_obj(dict, qstring_get_str(key), value);
    QDECREF(key);

    *length = ptr - data;

    return 0;

out:
    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, const char *data, size_t *length, va_list *ap)
{
    const char *ptr = data;
    QDict *dict = NULL;

    ptr += parse_skip(ctxt, ptr);

    if (*ptr != '{') {
        goto out;
    }
    ptr++;

    dict = qdict_new();

    ptr += parse_skip(ctxt, ptr);

    if (*ptr && *ptr != '}') {
        size_t len = 0;

        if (parse_pair(ctxt, dict, ptr, &len, ap) == -1) {
            goto out;
        }

        ptr += len;
    }

    ptr += parse_skip(ctxt, ptr);

    while (*ptr && *ptr != '}') {
        size_t len = 0;

        if (*ptr != ',') {
            parse_error(ctxt, ptr, "expected separator in dict");
            goto out;
        }
        ptr++;

        if (parse_pair(ctxt, dict, ptr, &len, ap) == -1) {
            goto out;
        }
        ptr += len;

        ptr += parse_skip(ctxt, ptr);
    }

    if (*ptr != '}') {
        parse_error(ctxt, ptr, "unterminated dict");
        goto out;
    }
    ptr++;

    *length = ptr - data;

    return QOBJECT(dict);

out:
    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, const char *data, size_t *length, va_list *ap)
{
    const char *ptr = data;
    QList *list = NULL;

    ptr += parse_skip(ctxt, ptr);

    if (*ptr != '[') {
        goto out;
    }
    ptr++;

    list = qlist_new();

    ptr += parse_skip(ctxt, ptr);

    if (*ptr && *ptr != ']') {
        size_t len = 0;
        QObject *obj;

        obj = parse_value(ctxt, ptr, &len, ap);
        if (obj == NULL) {
            parse_error(ctxt, ptr, "expecting value");
            goto out;
        }
        ptr += len;

        qlist_append_obj(list, obj);
    }

    ptr += parse_skip(ctxt, ptr);

    while (*ptr && *ptr != ']') {
        size_t len = 0;
        QObject *obj;

        if (*ptr != ',') {
            parse_error(ctxt, ptr, "expected separator in list");
            goto out;
        }
        ptr++;

        obj = parse_value(ctxt, ptr, &len, ap);
        if (obj == NULL) {
            parse_error(ctxt, ptr, "expecting value");
            goto out;
        }
        ptr += len;

        qlist_append_obj(list, obj);

        ptr += parse_skip(ctxt, ptr);
   }

    if (*ptr != ']') {
        parse_error(ctxt, ptr, "unterminated array");
        goto out;
    }
    ptr++;

    *length = ptr - data;

    return QOBJECT(list);

out:
    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt, const char *data, size_t *length)
{
    const char *ptr = data;
    QString *str;
    QObject *obj = NULL;

    ptr += parse_skip(ctxt, ptr);

    str = qstring_new();

    while (*ptr >= 'a' && *ptr <= 'z') {
        char buf[2];

        buf[0] = *ptr;
        buf[1] = 0;
        ptr++;

        qstring_append(str, buf);
    }

    if (strcmp(qstring_get_str(str), "true") == 0) {
        obj = QOBJECT(qbool_from_int(1));
    } else if (strcmp(qstring_get_str(str), "false") == 0) {
        obj = QOBJECT(qbool_from_int(0));
    }

    if (obj) {
        *length = ptr - data;
    }

    QDECREF(str);

    return obj;
}

static QObject *parse_value(JSONParserContext *ctxt, const char *string, size_t *length, va_list *ap)
{
    QObject *obj;

    obj = QOBJECT(parse_string(ctxt, string, length, ap));
    if (obj == NULL) {
        obj = parse_number(ctxt, string, length, ap);
    }
    if (obj == NULL) {
        obj = parse_object(ctxt, string, length, ap);
    }
    if (obj == NULL) {
        obj = parse_array(ctxt, string, length, ap);
    }
    if (obj == NULL) {
        obj = parse_keyword(ctxt, string, length);
    }

    return obj;
}

static QObject *parse_json(const char *string, size_t *length, va_list *ap)
{
    JSONParserContext ctxt = {};
    size_t dummy_length = 0;

    if (length == NULL) {
        length = &dummy_length;
    }

    ctxt.data = string;
    ctxt.lineno = 0;

    return parse_value(&ctxt, string, length, ap);
}

QObject *qobject_from_json(const char *string, size_t *length)
{
    return parse_json(string, length, NULL);
}

QObject *qobject_from_jsonf(const char *string, size_t *length, ...)
{
    QObject *obj;
    va_list ap;

    va_start(ap, length);
    obj = parse_json(string, length,&ap);
    va_end(ap);

    return obj;
}
