#include "compiler.h"
#include <assert.h>
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "lexer.h"
#include "object.h"

#define UNUSED(parameter) __attribute__((unused)) UNUSED_##parameter

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
    PREC_PRIMARY,
} Precedence;

typedef struct {
    Lexer *lexer;
    Chunk *chunk;

    bool had_error;
    bool is_panicking;
    Token previous;
    Token current;
} Parser;

typedef void (*ParseFn)(bool can_assign);

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

static void error_previous(const char *message) { error(p.previous.line, message); }
static void error_current(const char *message) { error(p.current.line, message); }

static void advance(void) {
    p.previous = p.current;
    for (;;) {
        p.current = next_token(p.lexer);
        if (p.current.type != TOKEN_ERROR) break;
        error_current(p.current.start);
    }
}

static void expect(TokenType type, const char *error_message) {
    if (p.current.type == type) {
        advance();
    } else {
        error_current(error_message);
    }
}

static bool match(TokenType type) {
    if (p.current.type != type) return false;
    advance();
    return true;
}

static Chunk *current_chunk(void) { return p.chunk; }

static uint8_t new_constant(Value constant) {
    uint32_t index = push_constant(current_chunk(), constant);
    if (index > UINT8_MAX) {
        error_previous("Too many constants in one chunk");
        return 0;
    }

    return index;
}

static uint8_t identifier_constant(Token token) {
    return new_constant(VALUE_OBJECT(copy_string(token.start, token.length)));
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
        error_previous("Expected expression");
        return;
    }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(p.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(p.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) error_current("Invalid assignment target");
}

static void expression(void) { parse_precedence(PREC_ASSIGNMENT); }

static void named_variable(Token token, bool can_assign) {
    uint8_t name = identifier_constant(token);
    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_byte2(OP_SET_GLOBAL, name);
    } else {
        emit_byte2(OP_GET_GLOBAL, name);
    }
}

static void nil(bool UNUSED(can_assign)) { emit_byte(OP_NIL); }
static void true_(bool UNUSED(can_assign)) { emit_byte(OP_TRUE); }
static void false_(bool UNUSED(can_assign)) { emit_byte(OP_FALSE); }
static void number(bool UNUSED(can_assign)) { emit_constant(VALUE_NUMBER(strtod(p.previous.start, NULL))); }
static void string(bool UNUSED(can_assign)) {
    emit_constant(VALUE_OBJECT(copy_string(p.previous.start + 1, p.previous.length - 2)));
}
static void variable(bool can_assign) { named_variable(p.previous, can_assign); }

static void grouping(bool UNUSED(can_assign)) {
    expression();
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after expression");
}

static void unary(bool UNUSED(can_assign)) {
    TokenType op = p.previous.type;

    parse_precedence(PREC_UNARY);

    switch (op) {
        case TOKEN_BANG:  emit_byte(OP_NOT); break;
        case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
        default:          UNREACHABLE();
    }
}

static void binary(bool UNUSED(can_assign)) {
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

static void conditional(bool UNUSED(can_assign)) {
    parse_precedence(PREC_ASSIGNMENT);
    expect(TOKEN_COLON, "Expected ':' after then branch of conditional (ternary) operator");
    parse_precedence(PREC_CONDITIONAL);

    // Temporary:
    ERROR_AT(p.previous.line, "Conditional ternary operator is not implemented yet, for now it just sums all operands");
    emit_byte(OP_ADD);
    emit_byte(OP_ADD);
}

static const ParseRule rules[TOKEN_COUNT] = {
    // clang-format off
    // token                  prefix,   infix,       precedence
    [TOKEN_NIL]           = { nil,      NULL,        PREC_NONE        },
    [TOKEN_TRUE]          = { true_,    NULL,        PREC_NONE        },
    [TOKEN_FALSE]         = { false_,   NULL,        PREC_NONE        },
    [TOKEN_NUMBER]        = { number,   NULL,        PREC_NONE        },
    [TOKEN_STRING]        = { string,   NULL,        PREC_NONE        },
    [TOKEN_IDENTIFIER]    = { variable, NULL,        PREC_NONE        },
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

// Skip tokens until the statement boundary, either semicolon or one of the keywords
// that begin statement, to continue parsing and report multiple errors at once.
static void synchronize(void) {
    p.is_panicking = false;

    while (p.current.type != TOKEN_EOF) {
        if (p.previous.type == TOKEN_SEMICOLON) return;

        switch (p.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN: return;
            default:           advance(); break;
        }
    }
}

static void expression_stmt(void) {
    expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    emit_byte(OP_POP);
}

static void print_stmt(void) {
    advance();
    expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after print statement");
    emit_byte(OP_PRINT);
}

static void statement(void) {
    switch (p.current.type) {
        case TOKEN_PRINT: print_stmt(); break;
        default:          expression_stmt(); break;
    }
}

static uint8_t var_name(const char *error_message) {
    expect(TOKEN_IDENTIFIER, error_message);
    return identifier_constant(p.previous);
}

static void var_decl(void) {
    advance();
    uint8_t global = var_name("Expected variable name after 'var'");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        // Value is implicitly nil.
        emit_byte(OP_NIL);
    }
    expect(TOKEN_SEMICOLON, "Expected ';' after variable declaration");

    emit_byte2(OP_DEFINE_GLOBAL, global);
}

static void declaration(void) {
    switch (p.current.type) {
        case TOKEN_VAR: var_decl(); break;
        default:        statement(); break;
    }
    if (p.is_panicking) synchronize();
}

bool compile(const char *source, Chunk *chunk) {
    init_lexer(source);

    p.chunk = chunk;

    advance();
    while (!match(TOKEN_EOF)) declaration();
    emit_byte(OP_RETURN);

#ifdef DEBUG_PRINT_BYTECODE
    if (!p.had_error) disassemble_chunk(current_chunk());
#endif

    return !p.had_error;
}
