/*
 * JSON Parser 
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

#include <stdarg.h>

#include "qemu-common.h"
#include "qstring.h"
#include "qint.h"
#include "qdict.h"
#include "qlist.h"
#include "qfloat.h"
#include "qbool.h"
#include "json-parser.h"
#include "json-lexer.h"

typedef struct JSONParserContext
{
} JSONParserContext;

#define BUG_ON(cond) assert(!(cond))

/**
 * TODO
 *
 * 0) make errors meaningful again
 * 1) add geometry information to tokens
 * 3) should we return a parsed size?
 * 4) deal with premature EOI
 */

static QObject *parse_value(JSONParserContext *ctxt, QListEntry **tokens, va_list *ap);

/**
 * Token manipulators
 *
 * tokens are dictionaries that contain a type, a string value, and geometry information
 * about a token identified by the lexer.  These are routines that make working with
 * these objects a bit easier.
 */
static const char *token_get_value(QObject *obj)
{
    return qdict_get_str(qobject_to_qdict(obj), "token");
}

static JSONTokenType token_get_type(QObject *obj)
{
    return qdict_get_int(qobject_to_qdict(obj), "type");
}

static int token_is_operator(QObject *obj, char op)
{
    const char *val;

    if (token_get_type(obj) != JSON_OPERATOR) {
        return 0;
    }

    val = token_get_value(obj);

    return (val[0] == op) && (val[1] == 0);
}

static int token_is_keyword(QObject *obj, const char *value)
{
    if (token_get_type(obj) != JSON_KEYWORD) {
        return 0;
    }

    return strcmp(token_get_value(obj), value) == 0;
}

static int token_is_escape(QObject *obj, const char *value)
{
    if (token_get_type(obj) != JSON_ESCAPE) {
        return 0;
    }

    return (strcmp(token_get_value(obj), value) == 0);
}

static QObject *tokens_peek(QListEntry **tokens)
{
    return (*tokens)->value;
}

static QObject *tokens_pop(QListEntry **tokens)
{
    QListEntry *entry = *tokens;
    QObject *token;

    token = entry->value;
    *tokens = qlist_next(entry);

    return token;
}

/**
 * Error handler
 */
static void GCC_FMT_ATTR(3, 4) parse_error(JSONParserContext *ctxt,
                                           QObject *token, const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    fprintf(stderr, "parse error: ");
    vfprintf(stderr, msg, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/**
 * String helpers
 *
 * These helpers are used to unescape strings.
 */
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
static QString *qstring_from_escaped_str(JSONParserContext *ctxt, QObject *token)
{
    const char *ptr = token_get_value(token);
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
            case 'f':
                qstring_append(str, "\f");
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
                        parse_error(ctxt, token,
                                    "invalid hex escape sequence in string");
                        goto out;
                    }
                    ptr++;
                }

                wchar_to_utf8(unicode_char, utf8_char, sizeof(utf8_char));
                qstring_append(str, utf8_char);
            }   break;
            default:
                parse_error(ctxt, token, "invalid escape sequence in string");
                goto out;
            }
        } else {
            char dummy[2];

            dummy[0] = *ptr++;
            dummy[1] = 0;

            qstring_append(str, dummy);
        }
    }

    return str;

out:
    QDECREF(str);
    return NULL;
}

/**
 * Parsing rules
 */
static int parse_pair(JSONParserContext *ctxt, QDict *dict, QListEntry **tokens, va_list *ap)
{
    QObject *key, *token = NULL, *value, *peek;
    QListEntry *entry = *tokens;

    peek = tokens_peek(&entry);
    key = parse_value(ctxt, &entry, ap);
    if (!key || qobject_type(key) != QTYPE_QSTRING) {
        parse_error(ctxt, peek, "key is not a string in object");
        goto out;
    }

    token = tokens_pop(&entry);
    if (!token_is_operator(token, ':')) {
        parse_error(ctxt, token, "missing : in object pair");
        goto out;
    }

    value = parse_value(ctxt, &entry, ap);
    if (value == NULL) {
        parse_error(ctxt, token, "Missing value in dict");
        goto out;
    }

    qdict_put_obj(dict, qstring_get_str(qobject_to_qstring(key)), value);

    qobject_decref(key);
    *tokens = entry;

    return 0;

out:
    qobject_decref(key);

    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, QListEntry **tokens, va_list *ap)
{
    QDict *dict = NULL;
    QObject *token, *peek;
    QListEntry *entry = *tokens;

    token = tokens_pop(&entry);
    if (!token_is_operator(token, '{')) {
        goto out;
    }

    dict = qdict_new();

    peek = tokens_peek(&entry);
    if (!token_is_operator(peek, '}')) {
        if (parse_pair(ctxt, dict, &entry, ap) == -1) {
            goto out;
        }

        token = tokens_pop(&entry);
        while (!token_is_operator(token, '}')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in dict");
                goto out;
            }
            token = NULL;

            if (parse_pair(ctxt, dict, &entry, ap) == -1) {
                goto out;
            }

            token = tokens_pop(&entry);
        }
        token = NULL;
    } else {
        tokens_pop(&entry);
        token = NULL;
    }

    *tokens = entry;

    return QOBJECT(dict);

out:
    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, QListEntry **tokens, va_list *ap)
{
    QList *list = NULL;
    QObject *token, *peek;
    QListEntry *entry = *tokens;

    token = tokens_pop(&entry);
    if (!token_is_operator(token, '[')) {
        goto out;
    }
    token = NULL;

    list = qlist_new();

    peek = tokens_peek(&entry);
    if (!token_is_operator(peek, ']')) {
        QObject *obj;

        obj = parse_value(ctxt, &entry, ap);
        if (obj == NULL) {
            parse_error(ctxt, peek, "expecting value");
            goto out;
        }

        qlist_append_obj(list, obj);

        token = tokens_pop(&entry);
        while (!token_is_operator(token, ']')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in list");
                goto out;
            }

            obj = parse_value(ctxt, &entry, ap);
            if (obj == NULL) {
                parse_error(ctxt, NULL, "expecting value");
                goto out;
            }

            qlist_append_obj(list, obj);

            token = tokens_pop(&entry);
        }
    } else {
        tokens_pop(&entry);
    }

    *tokens = entry;

    return QOBJECT(list);

out:
    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt, QListEntry **tokens)
{
    QObject *token, *ret;
    QListEntry *entry = *tokens;

    token = tokens_pop(&entry);

    if (token_get_type(token) != JSON_KEYWORD) {
        return NULL;
    }

    if (token_is_keyword(token, "true")) {
        ret = QOBJECT(qbool_from_int(true));
    } else if (token_is_keyword(token, "false")) {
        ret = QOBJECT(qbool_from_int(false));
    } else {
        parse_error(ctxt, token, "invalid keyword `%s'", token_get_value(token));
        return NULL;
    }

    *tokens = entry;

    return ret;
}

static QObject *parse_escape(JSONParserContext *ctxt, QListEntry **tokens, va_list *ap)
{
    QObject *token = NULL, *obj;
    QListEntry *entry = *tokens;

    if (ap == NULL) {
        return NULL;
    }

    token = tokens_pop(&entry);

    if (token_is_escape(token, "%p")) {
        obj = va_arg(*ap, QObject *);
    } else if (token_is_escape(token, "%i")) {
        obj = QOBJECT(qbool_from_int(va_arg(*ap, int)));
    } else if (token_is_escape(token, "%d")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, int)));
    } else if (token_is_escape(token, "%ld")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, long)));
    } else if (token_is_escape(token, "%lld") ||
               token_is_escape(token, "%I64d")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, long long)));
    } else if (token_is_escape(token, "%s")) {
        obj = QOBJECT(qstring_from_str(va_arg(*ap, const char *)));
    } else if (token_is_escape(token, "%f")) {
        obj = QOBJECT(qfloat_from_double(va_arg(*ap, double)));
    } else {
        return NULL;
    }

    *tokens = entry;

    return obj;
}

static QObject *parse_literal(JSONParserContext *ctxt, QListEntry **tokens)
{
    QObject *token, *obj;
    QListEntry *entry = *tokens;

    token = tokens_pop(&entry);
    switch (token_get_type(token)) {
    case JSON_STRING:
        obj = QOBJECT(qstring_from_escaped_str(ctxt, token));
        break;
    case JSON_INTEGER:
        obj = QOBJECT(qint_from_int(strtoll(token_get_value(token), NULL, 10)));
        break;
    case JSON_FLOAT:
        /* FIXME dependent on locale */
        obj = QOBJECT(qfloat_from_double(strtod(token_get_value(token), NULL)));
        break;
    default:
        return NULL;
    }

    *tokens = entry;

    return obj;
}

static QObject *parse_value(JSONParserContext *ctxt, QListEntry **tokens, va_list *ap)
{
    QObject *obj;

    obj = parse_object(ctxt, tokens, ap);
    if (obj == NULL) {
        obj = parse_array(ctxt, tokens, ap);
    }
    if (obj == NULL) {
        obj = parse_escape(ctxt, tokens, ap);
    }
    if (obj == NULL) {
        obj = parse_keyword(ctxt, tokens);
    } 
    if (obj == NULL) {
        obj = parse_literal(ctxt, tokens);
    }

    return obj;
}

QObject *json_parser_parse(QList *tokens, va_list *ap)
{
    JSONParserContext ctxt = {};
    QListEntry *working = qlist_first(tokens);

    return parse_value(&ctxt, &working, ap);
}
