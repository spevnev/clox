#include "compiler.h"
#include <assert.h>
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "lexer.h"
#include "object.h"

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

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static Parser p = {0};

static const ParseRule *get_rule(TokenType op);

static void error(uint32_t line, const char *message) {
    if (p.is_panicking) return;
    p.is_panicking = true;
    p.had_error = true;
    ERROR_AT(line, message);
}

static void advance() {
    p.previous = p.current;
    for (;;) {
        p.current = next_token(p.lexer);
        if (p.current.type != TOKEN_ERROR) break;
        error(p.current.line, p.current.start);
    }
}

static void expect(TokenType type, const char *error_message) {
    if (p.current.type == type) {
        advance();
    } else {
        error(p.current.line, error_message);
    }
}

static Chunk *current_chunk() { return p.chunk; }

static uint8_t new_constant(Value constant) {
    uint32_t index = push_constant(current_chunk(), constant);
    if (index > UINT8_MAX) {
        error(p.previous.line, "Too many constants in one chunk");
        return 0;
    }

    return index;
}

static void emit_byte(uint8_t byte) { push_byte(current_chunk(), byte, p.previous.line); }

static void emit_byte2(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_constant(Value constant) { emit_byte2(OP_CONSTANT, new_constant(constant)); }

static void parse_precedence(Precedence precedence) {
    advance();

    ParseFn prefix_rule = get_rule(p.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(p.previous.line, "Expected expression");
        return;
    }

    prefix_rule();

    while (precedence <= get_rule(p.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(p.previous.type)->infix;
        infix_rule();
    }
}

static void expression() { parse_precedence(PREC_ASSIGNMENT); }

static void nil() { emit_byte(OP_NIL); }
static void true_() { emit_byte(OP_TRUE); }
static void false_() { emit_byte(OP_FALSE); }
static void number() { emit_constant(VALUE_NUMBER(strtod(p.previous.start, NULL))); }
static void string() { emit_constant(VALUE_OBJECT(copy_string(p.previous.start + 1, p.previous.length - 2))); }

static void grouping() {
    expression();
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after expression");
}

static void unary() {
    TokenType op = p.previous.type;

    parse_precedence(PREC_UNARY);

    switch (op) {
        case TOKEN_BANG:  emit_byte(OP_NOT); break;
        case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
        default:          UNREACHABLE();
    }
}

static void binary() {
    TokenType op = p.previous.type;

    parse_precedence(get_rule(op)->precedence + 1);

    switch (op) {
        case TOKEN_PLUS:          emit_byte(OP_ADD); break;
        case TOKEN_MINUS:         emit_byte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emit_byte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emit_byte(OP_DIVIDE); break;
        case TOKEN_BANG_EQUAL:    emit_byte2(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emit_byte(OP_EQUAL); break;
        case TOKEN_GREATER:       emit_byte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emit_byte2(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emit_byte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emit_byte2(OP_GREATER, OP_NOT); break;
        default:                  UNREACHABLE();
    }
}

static void conditional() {
    parse_precedence(PREC_ASSIGNMENT);
    expect(TOKEN_COLON, "Expected ':' after after the then branch of conditional (ternary) operator");
    parse_precedence(PREC_CONDITIONAL);

    // Temporary:
    ERROR_AT(p.previous.line,
             "Conditional ternary operator is not implemented yet, for now it just sums all the results");
    emit_byte(OP_ADD);
    emit_byte(OP_SUBTRACT);
}

static const ParseRule rules[TOKEN_COUNT] = {
    // clang-format off
    // token                  prefix,   infix,       precedence
    [TOKEN_NIL]           = { nil,      NULL,        PREC_NONE        },
    [TOKEN_TRUE]          = { true_,    NULL,        PREC_NONE        },
    [TOKEN_FALSE]         = { false_,   NULL,        PREC_NONE        },
    [TOKEN_NUMBER]        = { number,   NULL,        PREC_NONE        },
    [TOKEN_STRING]        = { string,   NULL,        PREC_NONE        },
    [TOKEN_LEFT_PAREN]    = { grouping, NULL,        PREC_NONE        },
    [TOKEN_BANG]          = { unary,    NULL,        PREC_NONE        },
    [TOKEN_MINUS]         = { unary,    binary,      PREC_TERM        },
    [TOKEN_PLUS]          = { NULL,     binary,      PREC_TERM        },
    [TOKEN_SLASH]         = { NULL,     binary,      PREC_FACTOR      },
    [TOKEN_STAR]          = { NULL,     binary,      PREC_FACTOR      },
    [TOKEN_BANG_EQUAL]    = { NULL,     binary,      PREC_EQUALITY    },
    [TOKEN_EQUAL_EQUAL]   = { NULL,     binary,      PREC_EQUALITY    },
    [TOKEN_GREATER]       = { NULL,     binary,      PREC_COMPARISON  },
    [TOKEN_GREATER_EQUAL] = { NULL,     binary,      PREC_COMPARISON  },
    [TOKEN_LESS]          = { NULL,     binary,      PREC_COMPARISON  },
    [TOKEN_LESS_EQUAL]    = { NULL,     binary,      PREC_COMPARISON  },
    [TOKEN_QUESTION]      = { NULL,     conditional, PREC_CONDITIONAL },
    // clang-format on
};

static const ParseRule *get_rule(TokenType op) { return &rules[op]; }

bool compile(const char *source, Chunk *chunk) {
    init_lexer(source);

    p.chunk = chunk;

    advance();
    expression();
    expect(TOKEN_EOF, "Expected end of expressions");
    emit_byte(OP_RETURN);

#ifdef DEBUG_PRINT_BYTECODE
    if (!p.had_error) disassemble_chunk(current_chunk());
#endif

    return !p.had_error;
}
