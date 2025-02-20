#include "compiler.h"
#include <assert.h>
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "lexer.h"

typedef enum {
    PREC_NONE = 0,
    PREC_ASSIGNMENT,   // =
    PREC_CONDITIONAL,  // ?:
    PREC_OR,           // or
    PREC_AND,          // and
    PREC_EQUALITY,     // == !=
    PREC_COMPARISON,   // < > <= >=
    PREC_TERM,         // + -
    PREC_FACTOR,       // * /
    PREC_UNARY,        // ! -
    PREC_CALL,         // . ()
    PREC_PRIMARY
} Precedence;

typedef struct {
    Lexer *lexer;
    Chunk *chunk;

    bool had_error;
    bool is_panicking;
    Token previous;
    Token current;
} Parser;

typedef void (*ParseFn)(Parser *);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static const ParseRule *get_rule(TokenType op);

static void error(Parser *p, uint32_t line, const char *message) {
    if (p->is_panicking) return;
    p->is_panicking = true;
    p->had_error = true;
    ERROR_AT(line, message);
}

static void advance(Parser *p) {
    p->previous = p->current;
    for (;;) {
        p->current = next_token(p->lexer);
        if (p->current.type != TOKEN_ERROR) break;
        error(p, p->current.line, p->current.start);
    }
}

static void expect(Parser *p, TokenType type, const char *error_message) {
    if (p->current.type == type) {
        advance(p);
    } else {
        error(p, p->current.line, error_message);
    }
}

static Chunk *current_chunk(Parser *p) { return p->chunk; }

static uint8_t new_constant(Parser *p, Value constant) {
    uint32_t index = push_constant(current_chunk(p), constant);
    if (index > UINT8_MAX) {
        error(p, p->previous.line, "Too many constants in one chunk");
        return 0;
    }

    return index;
}

static void emit_byte(Parser *p, uint8_t byte) { push_byte(current_chunk(p), byte, p->previous.line); }

static void emit_byte2(Parser *p, uint8_t byte1, uint8_t byte2) {
    emit_byte(p, byte1);
    emit_byte(p, byte2);
}

static void emit_constant(Parser *p, Value constant) { emit_byte2(p, OP_CONSTANT, new_constant(p, constant)); }

static void emit_return(Parser *p) { emit_byte(p, OP_RETURN); }

static void parse_precedence(Parser *p, Precedence precedence) {
    advance(p);

    ParseFn prefix_rule = get_rule(p->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(p, p->previous.line, "Expected expression");
        return;
    }

    prefix_rule(p);

    while (precedence <= get_rule(p->current.type)->precedence) {
        advance(p);
        ParseFn infix_rule = get_rule(p->previous.type)->infix;
        infix_rule(p);
    }
}

static void expression(Parser *p) { parse_precedence(p, PREC_ASSIGNMENT); }

static void number(Parser *p) { emit_constant(p, strtod(p->previous.start, NULL)); }

static void grouping(Parser *p) {
    expression(p);
    expect(p, TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after expression");
}

static void unary(Parser *p) {
    TokenType op = p->previous.type;

    parse_precedence(p, PREC_UNARY);

    switch (op) {
        case TOKEN_MINUS: emit_byte(p, OP_NEGATE); break;
        default:          UNREACHABLE();
    }
}

static void binary(Parser *p) {
    TokenType op = p->previous.type;

    parse_precedence(p, get_rule(op)->precedence + 1);

    switch (op) {
        case TOKEN_PLUS:  emit_byte(p, OP_ADD); break;
        case TOKEN_MINUS: emit_byte(p, OP_SUBTRACT); break;
        case TOKEN_STAR:  emit_byte(p, OP_MULTIPLY); break;
        case TOKEN_SLASH: emit_byte(p, OP_DIVIDE); break;
        default:          UNREACHABLE();
    }
}

static void conditional(Parser *p) {
    parse_precedence(p, PREC_ASSIGNMENT);
    expect(p, TOKEN_COLON, "Expected ':' after after the then branch of conditional (ternary) operator");
    parse_precedence(p, PREC_CONDITIONAL);

    // Temporary:
    ERROR_AT(p->previous.line, "Conditional ternary operator is not implemented yet, for now it just sums all the results");
    emit_byte(p, OP_ADD);
    emit_byte(p, OP_SUBTRACT);
}

static const ParseRule rules[TOKEN_COUNT] = {
    // clang-format off
    // token               prefix,   infix,       precedence
    [TOKEN_LEFT_PAREN] = { grouping, NULL,        PREC_NONE        },
    [TOKEN_NUMBER]     = { number,   NULL,        PREC_NONE        },
    [TOKEN_MINUS]      = { unary,    binary,      PREC_TERM        },
    [TOKEN_PLUS]       = { NULL,     binary,      PREC_TERM        },
    [TOKEN_SLASH]      = { NULL,     binary,      PREC_FACTOR      },
    [TOKEN_STAR]       = { NULL,     binary,      PREC_FACTOR      },
    [TOKEN_QUESTION]   = { NULL,     conditional, PREC_CONDITIONAL },
    // clang-format on
};

static const ParseRule *get_rule(TokenType op) { return &rules[op]; }

bool compile(const char *source, Chunk *chunk) {
    Lexer lexer = {0};
    init_lexer(&lexer, source);

    Parser parser = {
        .lexer = &lexer,
        .chunk = chunk,
    };

    advance(&parser);
    expression(&parser);
    expect(&parser, TOKEN_EOF, "Expected end of expressions");
    emit_return(&parser);

#ifdef DEBUG_PRINT_BYTECODE
    if (!parser.had_error) disassemble_chunk(current_chunk(&parser));
#endif

    return !parser.had_error;
}
