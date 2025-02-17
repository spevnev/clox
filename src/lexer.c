#include "lexer.h"
#include <string.h>
#include "common.h"

static Token new_token(Lexer *l, TokenType type) {
    return ((Token) {
        .type = type,
        .start = l->start,
        .length = l->current - l->start,
        .line = l->line,
    });
}

static Token error_token(Lexer *l, const char *error_msg) {
    return ((Token) {
        .type = TOKEN_ERROR,
        .start = error_msg,
        .length = strlen(error_msg),
        .line = l->line,
    });
}

static bool is_digit(char c) { return '0' <= c && c <= '9'; }
static bool is_alpha(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_'; }
static bool is_alphanumeric(char c) { return is_alpha(c) || is_digit(c); }

static int token_len(Lexer *l) { return l->current - l->start; }
static bool is_done(Lexer *l) { return *l->current == '\0'; }
static char peek(Lexer *l) { return *l->current; }
static char peek_next(Lexer *l) { return *(l->current + 1); }
static char advance(Lexer *l) { return *(l->current++); }

static bool match(Lexer *l, char c) {
    if (is_done(l) || *l->current != c) return false;
    l->current++;
    return true;
}

static void skip_whitespace(Lexer *l) {
    for (;;) {
        switch (peek(l)) {
            case ' ':
            case '\r':
            case '\t': advance(l); break;
            case '\n':
                advance(l);
                l->line++;
                break;
            case '/':
                if (peek_next(l) == '/') {
                    while (!is_done(l) && peek(l) != '\n') advance(l);
                    break;
                } else {
                    return;
                }
            default: return;
        }
    }
}

static Token string(Lexer *l) {
    while (!is_done(l) && peek(l) != '"') {
        if (peek(l) == '\n') l->line++;
        advance(l);
    }

    if (match(l, '"')) {
        return new_token(l, TOKEN_STRING);
    } else {
        return error_token(l, "Unterminated string");
    }
}

static Token number(Lexer *l) {
    while (is_digit(peek(l))) advance(l);
    if (peek(l) == '.' && is_digit(peek_next(l))) {
        advance(l);  // Consume the dot
        while (is_digit(peek(l))) advance(l);
    }

    return new_token(l, TOKEN_NUMBER);
}

static TokenType check_keyword(Lexer *l, int offset, int length, const char *rest, TokenType type) {
    if (token_len(l) == offset + length && memcmp(l->start + offset, rest, length) == 0) return type;
    return TOKEN_IDENTIFIER;
}

static TokenType get_identifier_type(Lexer *l) {
    switch (l->start[0]) {
        case 'a': return check_keyword(l, 1, 2, "nd", TOKEN_AND);
        case 'c': return check_keyword(l, 1, 4, "lass", TOKEN_CLASS);
        case 'e': return check_keyword(l, 1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (token_len(l) > 1) {
                switch (l->start[1]) {
                    case 'a': return check_keyword(l, 2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(l, 2, 1, "r", TOKEN_FOR);
                    case 'u': return check_keyword(l, 2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return check_keyword(l, 1, 1, "f", TOKEN_IF);
        case 'n': return check_keyword(l, 1, 2, "il", TOKEN_NIL);
        case 'o': return check_keyword(l, 1, 1, "r", TOKEN_OR);
        case 'p': return check_keyword(l, 1, 4, "rint", TOKEN_PRINT);
        case 'r': return check_keyword(l, 1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword(l, 1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (token_len(l) > 1) {
                switch (l->start[1]) {
                    case 'h': return check_keyword(l, 2, 2, "is", TOKEN_THIS);
                    case 'r': return check_keyword(l, 2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return check_keyword(l, 1, 2, "ar", TOKEN_VAR);
        case 'w': return check_keyword(l, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(Lexer *l) {
    while (is_alphanumeric(peek(l))) advance(l);
    return new_token(l, get_identifier_type(l));
}

void init_lexer(Lexer *l, const char *source) {
    l->current = l->start = source;
    l->line = 1;
}

Token next_token(Lexer *l) {
    skip_whitespace(l);

    l->start = l->current;
    if (is_done(l)) return new_token(l, TOKEN_EOF);

    char c = advance(l);
    switch (c) {
        case '(': return new_token(l, TOKEN_LEFT_PAREN);
        case ')': return new_token(l, TOKEN_RIGHT_PAREN);
        case '{': return new_token(l, TOKEN_LEFT_BRACE);
        case '}': return new_token(l, TOKEN_RIGHT_BRACE);
        case ';': return new_token(l, TOKEN_SEMICOLON);
        case ',': return new_token(l, TOKEN_COMMA);
        case '.': return new_token(l, TOKEN_DOT);
        case '-': return new_token(l, TOKEN_MINUS);
        case '+': return new_token(l, TOKEN_PLUS);
        case '/': return new_token(l, TOKEN_SLASH);
        case '*': return new_token(l, TOKEN_STAR);
        case '!': return new_token(l, match(l, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return new_token(l, match(l, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return new_token(l, match(l, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return new_token(l, match(l, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string(l);
        default:
            if (is_digit(c)) return number(l);
            if (is_alpha(c)) return identifier(l);
            break;
    }

    return error_token(l, "Unexpected character");
}
