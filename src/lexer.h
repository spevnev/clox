#ifndef CLOX_LEXER_H_
#define CLOX_LEXER_H_

#include "chunk.h"
#include "common.h"

typedef enum {
    // Operators
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_PLUS,
    TOKEN_PLUS_PLUS,
    TOKEN_MINUS,
    TOKEN_MINUS_MINUS,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_SLASH,
    TOKEN_STAR,
    TOKEN_QUESTION,
    TOKEN_BANG,
    TOKEN_BANG_EQUAL,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_TEMPLATE_START,
    TOKEN_TEMPLATE_END,
    TOKEN_NUMBER,

    // Keywords
    TOKEN_AND,
    TOKEN_BREAK,
    TOKEN_CLASS,
    TOKEN_CONTINUE,
    TOKEN_ELSE,
    TOKEN_FALSE,
    TOKEN_FOR,
    TOKEN_FUN,
    TOKEN_IF,
    TOKEN_NIL,
    TOKEN_OR,
    TOKEN_PRINT,
    TOKEN_RETURN,
    TOKEN_SUPER,
    TOKEN_THIS,
    TOKEN_TRUE,
    TOKEN_VAR,
    TOKEN_WHILE,

    // Special
    TOKEN_ERROR,
    TOKEN_EOF,

    TOKEN_COUNT
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    uint32_t length;
    Loc loc;
} Token;

typedef struct {
    const char *start;
    const char *current;
    const char *line_start;
    uint32_t line;
    uint32_t template_count;
} Lexer;

void init_lexer(const char *source);
Token next_token(void);

#endif  // CLOX_LEXER_H_
