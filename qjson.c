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
static QString *qstring_from_escaped_str(const char *data)
{
    const char *ptr = data;
    QString *str;
    int double_quote = 1;

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

    ptr++;

    return str;

out:
    QDECREF(str);
    return NULL;
}

static QObject *token_next(QList *consumed, QList *remaining)
{
}

static const char *token_get_value(QObject *obj)
{
}

static JSONTokenType token_get_type(QObject *obj)
{
}

static void tokens_commit(QList *consumed, QList *working)
{
}

static void tokens_reset(QList *remaining, QList *working)
{
}

static int parse_pair(JSONParserContext *ctxt, QDict *dict, QList *consumed, QList *remaining, va_list *ap)
{
    const char *key;
    QObject *token;
    QList *working = qlist_new();

    token = token_next(working, remaining);
    if (!check_type(token, JSON_STRING)) {
        parse_error(ctxt, "key is not a string in object");
        goto out;
    }
    key = token_get_value(token);

    token = next_token(working, remaining);
    if (!check_operator(token, ':')) {
        parse_error(ctxt, "missing : in object pair");
        goto out;
    }

    value = parse_value(ctxt, working, remaining, ap);
    if (value == NULL) {
        parse_error(ctxt, "Missing value in dict");
        goto out;
    }

    qdict_put_obj(dict, key, value);

    tokens_commit(consumed, working);

    return 0;

out:
    tokens_reset(remaining, working);

    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, QList *consumed, QList *remaining, va_list *ap)
{
    QDict *dict = NULL;
    QList *working = qlist_new();
    QObject *token;

    token = next_token(working, remaining);
    if (!check_operator(token, '{')) {
        goto out;
    }

    dict = qdict_new();

    token = token_next(working, remaining);
    if (!token_is_operator(token, '}')) {
        if (parse_pair(ctxt, dict, working, remaining, ap) == -1) {
            goto out;
        }

        token = token_next(working, remaining);
        while (!token_is_operator(token, '}')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, ptr, "expected separator in dict");
                goto out;
            }

            if (parse_pair(ctxt, dict, working, remaining, ap) == -1) {
                goto out;
            }

            token = token_next(working, remaining);
        }
    }

    tokens_commit(consumed, working);

    return QOBJECT(dict);

out:
    tokens_reset(remaining, working);

    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, QList *consumed, QList *remaining, va_list *ap)
{
    QList *list = NULL;
    QObject *token;
    QList *working = qlist_new();

    token = token_next(working, remaining);
    if (!token_is_operator(token, '[')) {
        goto out;
    }

    list = qlist_new();

    token = token_next(working, remaining);
    if (!token_is_operator(token, ']')) {
        QObject *obj;

        obj = parse_value(ctxt, working, remaining, ap);
        if (obj == NULL) {
            parse_error(ctxt, ptr, "expecting value");
            goto out;
        }

        qlist_append_obj(list, obj);

        token = token_next(working, remaining);
        while (!token_is_operator(token, ']')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, ptr, "expected separator in list");
                goto out;
            }

            obj = parse_value(ctxt, working, remaining, ap);
            if (obj == NULL) {
                parse_error(ctxt, ptr, "expecting value");
                goto out;
            }

            token = token_next(working, remaining);
        }
    }

    tokens_commit(consumed, working);

    return QOBJECT(list);

out:
    tokens_reset(remaining, working);

    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt, QList *consumed, QList *remaining)
{
    QObject *token, *ret;
    QList *working = qlist_new();

    token = next_token(consumed, remaining);

    if (check_keyword(token, "true")) {
        ret = QOBJECT(qbool_from_int(true));
    } else if (check_keyword(token, "false")) {
        ret = QOBJECT(qbool_from_int(false));
    } else if (check_type(token, JSON_KEYWORD)) {
        parse_error(ctxt, "invalid keyword `%s'", qstring_get_str(tokeN));
        goto out;
    }

    qlist_append_list(consumed, working);

    return ret;

out: 
    qlist_prepend_list(remaining, working);

    return NULL;
}

static QObject *parse_escape(JSONParserContext *ctxt, QList *consumed, QList *remaining, va_list *ap)
{
    QObject *token, *obj;
    QList *working = qlist_new();

    token = next_token(consumed, remaining);

    if (check_escape(token, "%p")) {
        obj = va_arg(*ap, QObject *);
        qobject_incref(obj);
    } else if (check_escape(token, "%i")) {
        obj = qbool_from_int(va_arg(*ap, int));
    } else if (check_escape(token, "%d")) {
        obj = qint_from_int(va_arg(*ap, int));
    } else if (check_escape(token, "%ld")) {
        obj = qint_from_int(va_arg(*ap, long));
    } else if (check_escape(token, "%lld")) {
        obj = qint_from_int(va_arg(*ap, long long));
    } else {
        goto out;
    }

    qlist_append_list(consumed, working);

    return obj;

out:
    qlist_prepend_list(remaining, working);

    return NULL;
}

static QObject *parse_literal(JSONParserContext *ctxt, QList *consumed, QList *remaining)
{
    QList *working = qlist_new();
    QObject *token, *obj;

    token = next_token(working, remaining);
    if (check_type(token, JSON_STRING)) {
        obj = qstring_from_escaped_str(token_get_value(token));
    } else if (check_type(token, JSON_INTEGER)) {
        obj = qint_from_int(strtoll(token_get_value(token), NULL, 10));
    } else if (check_type(token, JSON_FLOAT)) {
        obj = qfloat_from_double(strtod(token_get_value(token), NULL));
    } else {
        goto out;
    }

    qlist_append_list(consumed, working);

    return obj;

out:
    qlist_prepend_list(remaining, working);

    return NULL;
}

static QObject *parse_value(JSONParserContext *ctxt, QList *consumed, QList *remaining, va_list *ap)
{
    QObject *obj;

    obj = parse_object(ctxt, consumed, remaining, ap);
    if (obj == NULL) {
        obj = parse_array(ctxt, consumed, remaining, ap);
    }
    if (obj == NULL) {
        obj = parse_escape(ctxt, consumed, remaining, ap);
    }
    if (obj == NULL) {
        obj = parse_keyword(ctxt, consumed, remaining);
    } 
    if (obj == NULL) {
        obj = parse_literal(ctxt, consumed, remaining);
    }

    return obj;
}

static QObject *parse_json(QList *tokens, va_list *ap)
{
    JSONParserContext ctxt = {};
    QList *consumed = qlist_new();

    return parse_value(&ctxt, consumed, tokens, ap);
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
