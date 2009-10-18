#include "qstring.h"
#include "qlist.h"
#include "qdict.h"
#include "qint.h"
#include "qemu-common.h"

/*
 * \"([^\\\"]|(\\\"\\'\\\\\\/\\b\\f\\n\\r\\t\\u[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]))*\"
 * '([^\\']|(\\\"\\'\\\\\\/\\b\\f\\n\\r\\t\\u[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]))*'
 * 0|([1-9][0-9]*(.[0-9]+)?([eE]([-+])?[0-9]+))
 * [{}\[\],:]
 * [a-z]+
 *
 */

/* FIXME
 * 1) support %
 * 2) tokenize properly
 * 3) convert parser to use lexer
 */

typedef enum json_token_type {
    JSON_OPERATOR = 100,
    JSON_NUMBER,
    JSON_KEYWORD,
    JSON_STRING,
    JSON_SKIP,
} JSONTokenType;
    
enum json_lexer_state {
    ERROR = 0,
    IN_DONE_STRING,
    IN_DQ_UCODE3,
    IN_DQ_UCODE2,
    IN_DQ_UCODE1,
    IN_DQ_UCODE0,
    IN_DQ_STRING_ESCAPE,
    IN_DQ_STRING,
    IN_SQ_UCODE3,
    IN_SQ_UCODE2,
    IN_SQ_UCODE1,
    IN_SQ_UCODE0,
    IN_SQ_STRING_ESCAPE,
    IN_SQ_STRING,
    IN_ZERO,
    IN_DIGITS,
    IN_DIGIT,
    IN_EXP_E,
    IN_MANTISSA,
    IN_MANTISSA_DIGITS,
    IN_NONZERO_NUMBER,
    IN_NEG_NONZERO_NUMBER,
    IN_KEYWORD,
    IN_WHITESPACE,
    IN_OPERATOR_DONE,
    IN_START,
};

#define TERMINAL(state) [0 ... 0x7F] = (state)

static const uint8_t json_lexer[][256] =  {
    [IN_DONE_STRING] = {
        TERMINAL(JSON_STRING),
    },

    /* double quote string */
    [IN_DQ_UCODE3] = {
        ['0' ... '9'] = IN_DQ_STRING,
        ['a' ... 'f'] = IN_DQ_STRING,
        ['A' ... 'F'] = IN_DQ_STRING,
    },
    [IN_DQ_UCODE2] = {
        ['0' ... '9'] = IN_DQ_UCODE3,
        ['a' ... 'f'] = IN_DQ_UCODE3,
        ['A' ... 'F'] = IN_DQ_UCODE3,
    },
    [IN_DQ_UCODE1] = {
        ['0' ... '9'] = IN_DQ_UCODE2,
        ['a' ... 'f'] = IN_DQ_UCODE2,
        ['A' ... 'F'] = IN_DQ_UCODE2,
    },
    [IN_DQ_UCODE0] = {
        ['0' ... '9'] = IN_DQ_UCODE1,
        ['a' ... 'f'] = IN_DQ_UCODE1,
        ['A' ... 'F'] = IN_DQ_UCODE1,
    },
    [IN_DQ_STRING_ESCAPE] = {
        ['b'] = IN_DQ_STRING,
        ['f'] =  IN_DQ_STRING,
        ['n'] =  IN_DQ_STRING,
        ['r'] =  IN_DQ_STRING,
        ['t'] =  IN_DQ_STRING,
        ['\''] = IN_DQ_STRING,
        ['\"'] = IN_DQ_STRING,
        ['u'] = IN_DQ_UCODE0,
    },
    [IN_DQ_STRING] = {
        [1 ... 0xFF] = IN_DQ_STRING,
        ['\\'] = IN_DQ_STRING_ESCAPE,
        ['"'] = IN_DONE_STRING,
    },

    /* single quote string */
    [IN_SQ_UCODE3] = {
        ['0' ... '9'] = IN_SQ_STRING,
        ['a' ... 'f'] = IN_SQ_STRING,
        ['A' ... 'F'] = IN_SQ_STRING,
    },
    [IN_SQ_UCODE2] = {
        ['0' ... '9'] = IN_SQ_UCODE3,
        ['a' ... 'f'] = IN_SQ_UCODE3,
        ['A' ... 'F'] = IN_SQ_UCODE3,
    },
    [IN_SQ_UCODE1] = {
        ['0' ... '9'] = IN_SQ_UCODE2,
        ['a' ... 'f'] = IN_SQ_UCODE2,
        ['A' ... 'F'] = IN_SQ_UCODE2,
    },
    [IN_SQ_UCODE0] = {
        ['0' ... '9'] = IN_SQ_UCODE1,
        ['a' ... 'f'] = IN_SQ_UCODE1,
        ['A' ... 'F'] = IN_SQ_UCODE1,
    },
    [IN_SQ_STRING_ESCAPE] = {
        ['b'] = IN_SQ_STRING,
        ['f'] =  IN_SQ_STRING,
        ['n'] =  IN_SQ_STRING,
        ['r'] =  IN_SQ_STRING,
        ['t'] =  IN_SQ_STRING,
        ['\''] = IN_SQ_STRING,
        ['\"'] = IN_SQ_STRING,
        ['u'] = IN_SQ_UCODE0,
    },
    [IN_SQ_STRING] = {
        [1 ... 0xFF] = IN_SQ_STRING,
        ['\\'] = IN_SQ_STRING_ESCAPE,
        ['\''] = IN_DONE_STRING,
    },

    /* Zero */
    [IN_ZERO] = {
        TERMINAL(JSON_NUMBER),
        ['0' ... '9'] = ERROR,
    },

    /* Non-zero numbers */
    [IN_DIGITS] = {
        TERMINAL(JSON_NUMBER),
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_DIGIT] = {
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_EXP_E] = {
        ['-'] = IN_DIGIT,
        ['+'] = IN_DIGIT,
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_MANTISSA_DIGITS] = {
        TERMINAL(JSON_NUMBER),
        ['0' ... '9'] = IN_MANTISSA_DIGITS,
        ['e'] = IN_EXP_E,
        ['E'] = IN_EXP_E,
    },

    [IN_MANTISSA] = {
        ['0' ... '9'] = IN_MANTISSA_DIGITS,
    },

    [IN_NONZERO_NUMBER] = {
        TERMINAL(JSON_NUMBER),
        ['0' ... '9'] = IN_NONZERO_NUMBER,
        ['e'] = IN_EXP_E,
        ['E'] = IN_EXP_E,
        ['.'] = IN_MANTISSA,
    },

    [IN_NEG_NONZERO_NUMBER] = {
        ['1' ... '9'] = IN_NONZERO_NUMBER,
    },

    /* keywords */
    [IN_KEYWORD] = {
        TERMINAL(JSON_KEYWORD),
        ['a' ... 'z'] = IN_KEYWORD,
    },

    /* whitespace */
    [IN_WHITESPACE] = {
        TERMINAL(JSON_SKIP),
        [' '] = IN_WHITESPACE,
        ['\t'] = IN_WHITESPACE,
        ['\r'] = IN_WHITESPACE,
        ['\n'] = IN_WHITESPACE,
    },        

    /* operator */
    [IN_OPERATOR_DONE] = {
        TERMINAL(JSON_OPERATOR),
    },

    /* top level rule */
    [IN_START] = {
        ['"'] = IN_DQ_STRING,
        ['\''] = IN_SQ_STRING,
        ['0'] = IN_ZERO,
        ['1' ... '9'] = IN_NONZERO_NUMBER,
        ['-'] = IN_NEG_NONZERO_NUMBER,
        ['{'] = IN_OPERATOR_DONE,
        ['}'] = IN_OPERATOR_DONE,
        ['['] = IN_OPERATOR_DONE,
        [']'] = IN_OPERATOR_DONE,
        [','] = IN_OPERATOR_DONE,
        [':'] = IN_OPERATOR_DONE,
        ['a' ... 'z'] = IN_KEYWORD,
        [' '] = IN_WHITESPACE,
        ['\t'] = IN_WHITESPACE,
        ['\r'] = IN_WHITESPACE,
        ['\n'] = IN_WHITESPACE,
    },
};

typedef struct JSONLexer JSONLexer;

typedef void (JSONLexerEmitter)(JSONLexer *, QString *, JSONTokenType);

struct JSONLexer
{
    JSONLexerEmitter *emit;
    enum json_lexer_state state;
    QString *token;
};

void json_lexer_init(JSONLexer *lexer, JSONLexerEmitter func)
{
    lexer->emit = func;
    lexer->state = IN_START;
    lexer->token = qstring_new();
}

static int json_lexer_feed_char(JSONLexer *lexer, char ch)
{
    char buf[2];

    lexer->state = json_lexer[lexer->state][(uint8_t)ch];

    switch (lexer->state) {
    case JSON_OPERATOR:
    case JSON_NUMBER:
    case JSON_KEYWORD:
    case JSON_STRING:
        lexer->emit(lexer, lexer->token, lexer->state);
    case JSON_SKIP:
        lexer->state = json_lexer[IN_START][(uint8_t)ch];
        QDECREF(lexer->token);
        lexer->token = qstring_new();
        break;
    case ERROR:
        return -EINVAL;
    default:
        break;
    }

    buf[0] = ch;
    buf[1] = 0;

    qstring_append(lexer->token, buf);

    return 0;
}

int json_lexer_feed(JSONLexer *lexer, const char *buffer, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        int err;

        err = json_lexer_feed_char(lexer, buffer[i]);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

typedef struct JSONMessageParser
{
    void (*emit)(struct JSONMessageParser *parser, QList *tokens);
    JSONLexer lexer;
    int brace_count;
    int bracket_count;
    QList *tokens;
} JSONMessageParser;

static void json_message_process_token(JSONLexer *lexer, QString *token, JSONTokenType type)
{
    JSONMessageParser *parser = container_of(lexer, JSONMessageParser, lexer);
    QDict *dict;

    if (type == JSON_OPERATOR) {
        switch (qstring_get_str(token)[0]) {
        case '{':
            parser->brace_count++;
            break;
        case '}':
            parser->brace_count--;
            break;
        case '[':
            parser->bracket_count++;
            break;
        case ']':
            parser->bracket_count--;
            break;
        default:
            break;
        }
    }

    dict = qdict_new();
    qdict_put_obj(dict, "type", QOBJECT(qint_from_int(type)));
    QINCREF(token);
    qdict_put_obj(dict, "token", QOBJECT(token));

    qlist_append(parser->tokens, dict);

    if (parser->brace_count == 0 &&
        parser->bracket_count == 0) {
        parser->emit(parser, parser->tokens);
        QDECREF(parser->tokens);
        parser->tokens = qlist_new();
    }
}

void json_message_parser_init(JSONMessageParser *parser,
                              void (*func)(JSONMessageParser *, QList *))
{
    parser->emit = func;
    parser->brace_count = 0;
    parser->bracket_count = 0;
    parser->tokens = qlist_new();

    json_lexer_init(&parser->lexer, json_message_process_token);
}

int json_message_parser_feed(JSONMessageParser *parser,
                             const char *buffer, size_t size)
{
    return json_lexer_feed(&parser->lexer, buffer, size);
}

static void got_message(JSONMessageParser *parser, QList *tokens)
{
    QListEntry *entry;

    qlist_foreach(entry, tokens) {
        QDict *dict = qobject_to_qdict(entry->value);
//        JSONTokenType type = qdict_get_int(dict, "type");
        QString *token = qobject_to_qstring(qdict_get(dict, "token"));

        printf("got token '%s'\n", qstring_get_str(token));
    }
}

int main(int argc, char **argv)
{
    JSONMessageParser parser = {};
    char buf[2];
    int ch;

    json_message_parser_init(&parser, got_message);

    while ((ch = getchar()) != EOF) {
        buf[0] = ch;
        buf[1] = 0;

        if (json_message_parser_feed(&parser, buf, 1) < 0) {
            fprintf(stderr, "Invalid character `%c'\n", ch);
            return 1;
        }
    }

    return 0;
}
