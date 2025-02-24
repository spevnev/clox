#include "lexer.h"
#include <string.h>
#include "common.h"

static Lexer l = {0};

static Token new_token(TokenType type) {
    return ((Token) {
        .type = type,
        .start = l.start,
        .length = l.current - l.start,
        .line = l.line,
    });
}

static Token error_token(const char *error_msg) {
    return ((Token) {
        .type = TOKEN_ERROR,
        .start = error_msg,
        .length = strlen(error_msg),
        .line = l.line,
    });
}

static bool is_digit(char c) { return '0' <= c && c <= '9'; }
static bool is_alpha(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_'; }
static bool is_alphanumeric(char c) { return is_alpha(c) || is_digit(c); }

static int token_length() { return l.current - l.start; }
static bool is_done() { return *l.current == '\0'; }
static char peek() { return *l.current; }
static char peek_next() { return *(l.current + 1); }
static char advance() { return *(l.current++); }

static bool match(char c) {
    if (is_done() || *l.current != c) return false;
    l.current++;
    return true;
}

static void skip_whitespace() {
    for (;;) {
        switch (peek()) {
            case ' ':
            case '\r':
            case '\t': advance(); break;
            case '\n':
                advance();
                l.line++;
                break;
            case '/':
                if (peek_next() == '/') {
                    while (!is_done() && peek() != '\n') advance();
                    break;
                } else {
                    return;
                }
            default: return;
        }
    }
}

static Token string() {
    while (!is_done() && peek() != '"') {
        if (peek() == '\n') l.line++;
        advance();
    }

    if (match('"')) {
        return new_token(TOKEN_STRING);
    } else {
        return error_token("Unterminated string");
    }
}

static Token number() {
    while (is_digit(peek())) advance();
    if (peek() == '.' && is_digit(peek_next())) {
        advance();  // Consume the dot
        while (is_digit(peek())) advance();
    }

    return new_token(TOKEN_NUMBER);
}

static TokenType check_keyword(int offset, int length, const char *rest, TokenType type) {
    if (token_length() == offset + length && memcmp(l.start + offset, rest, length) == 0) return type;
    return TOKEN_IDENTIFIER;
}

static TokenType get_identifier_type() {
    switch (l.start[0]) {
        case 'a': return check_keyword(1, 2, "nd", TOKEN_AND);
        case 'c': return check_keyword(1, 4, "lass", TOKEN_CLASS);
        case 'e': return check_keyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (token_length() > 1) {
                switch (l.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return check_keyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return check_keyword(1, 1, "f", TOKEN_IF);
        case 'n': return check_keyword(1, 2, "il", TOKEN_NIL);
        case 'o': return check_keyword(1, 1, "r", TOKEN_OR);
        case 'p': return check_keyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (token_length() > 1) {
                switch (l.start[1]) {
                    case 'h': return check_keyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return check_keyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return check_keyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (is_alphanumeric(peek())) advance();
    return new_token(get_identifier_type());
}

void init_lexer(const char *source) {
    l.current = l.start = source;
    l.line = 1;
}

Token next_token() {
    skip_whitespace();

    l.start = l.current;
    if (is_done()) return new_token(TOKEN_EOF);

    char c = advance();
    switch (c) {
        case '(': return new_token(TOKEN_LEFT_PAREN);
        case ')': return new_token(TOKEN_RIGHT_PAREN);
        case '{': return new_token(TOKEN_LEFT_BRACE);
        case '}': return new_token(TOKEN_RIGHT_BRACE);
        case ';': return new_token(TOKEN_SEMICOLON);
        case ':': return new_token(TOKEN_COLON);
        case ',': return new_token(TOKEN_COMMA);
        case '.': return new_token(TOKEN_DOT);
        case '-': return new_token(TOKEN_MINUS);
        case '+': return new_token(TOKEN_PLUS);
        case '/': return new_token(TOKEN_SLASH);
        case '*': return new_token(TOKEN_STAR);
        case '?': return new_token(TOKEN_QUESTION);
        case '!': return new_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return new_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return new_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return new_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
        default:
            if (is_digit(c)) return number();
            if (is_alpha(c)) return identifier();
            break;
    }

    return error_token("Unexpected character");
}
