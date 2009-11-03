#ifndef QEMU_JSON_LEXER_H
#define QEMU_JSON_LEXER_H

#include "qstring.h"
#include "qlist.h"

typedef enum json_token_type {
    JSON_OPERATOR = 100,
    JSON_INTEGER,
    JSON_FLOAT,
    JSON_KEYWORD,
    JSON_STRING,
    JSON_ESCAPE,
    JSON_SKIP,
} JSONTokenType;

typedef struct JSONLexer JSONLexer;

typedef void (JSONLexerEmitter)(JSONLexer *, QString *, JSONTokenType, int x, int y);

struct JSONLexer
{
    JSONLexerEmitter *emit;
    int state;
    QString *token;
    int x, y;
};

typedef struct JSONMessageParser
{
    void (*emit)(struct JSONMessageParser *parser, QList *tokens);
    JSONLexer lexer;
    int brace_count;
    int bracket_count;
    QList *tokens;
} JSONMessageParser;

void json_lexer_init(JSONLexer *lexer, JSONLexerEmitter func);

int json_lexer_feed(JSONLexer *lexer, const char *buffer, size_t size);

int json_lexer_flush(JSONLexer *lexer);

void json_lexer_destroy(JSONLexer *lexer);

void json_message_parser_init(JSONMessageParser *parser,
                              void (*func)(JSONMessageParser *, QList *));

int json_message_parser_feed(JSONMessageParser *parser,
                             const char *buffer, size_t size);

int json_message_parser_flush(JSONMessageParser *parser);

void json_message_parser_destroy(JSONMessageParser *parser);

#endif
