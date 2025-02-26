#include "compiler.h"
#include <assert.h>
#include <string.h>
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
    PREC_PRIMARY,
} Precedence;

typedef struct {
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

typedef struct {
    Token name;
    int depth;
} Local;

#define DEPTH_UNINITIALIZED -1

typedef struct {
    Local locals[UINT8_MAX + 1];
    int local_count;
    int scope_depth;
} Compiler;

static Parser p = {0};
static Compiler *c = NULL;

static const ParseRule *get_rule(TokenType op);
static void declaration(void);

#define ERROR(line, ...)                 \
    do {                                 \
        if (!p.is_panicking) {           \
            p.is_panicking = true;       \
            p.had_error = true;          \
            ERROR_AT(line, __VA_ARGS__); \
        }                                \
    } while (0)
#define ERROR_PREV(...) ERROR(p.previous.line, __VA_ARGS__)
#define ERROR_CURRENT(...) ERROR(p.current.line, __VA_ARGS__)

static bool is_next(TokenType type) { return p.current.type == type; }

static void advance(void) {
    p.previous = p.current;
    for (;;) {
        p.current = next_token();
        if (p.current.type != TOKEN_ERROR) break;
        ERROR_CURRENT(p.current.start);
    }
}

static void expect(TokenType type, const char *error_message) {
    if (p.current.type == type) {
        advance();
    } else {
        ERROR_CURRENT(error_message);
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
        ERROR_PREV("Too many constants in one chunk");
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
        ERROR_PREV("Expected expression");
        return;
    }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(p.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(p.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) ERROR_CURRENT("Invalid assignment target");
}

static void expression(void) { parse_precedence(PREC_ASSIGNMENT); }

static bool token_equals(Token a, Token b) { return a.length == b.length && memcmp(a.start, b.start, a.length) == 0; }

static int resolve_local(Compiler *c, Token name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (!token_equals(c->locals[i].name, name)) continue;
        if (c->locals[i].depth == DEPTH_UNINITIALIZED) {
            ERROR_PREV("Cannot read local variable '%.*s' in its own initializer", name.length, name.start);
        }
        return i;
    }
    return -1;
}

static void named_var(Token token, bool can_assign) {
    int local_idx = resolve_local(c, token);

    uint8_t get_op, set_op, operand;
    if (local_idx == -1) {
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
        operand = identifier_constant(token);
    } else {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
        operand = local_idx;
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_byte2(set_op, operand);
    } else {
        emit_byte2(get_op, operand);
    }
}

static void nil(bool UNUSED(can_assign)) { emit_byte(OP_NIL); }
static void true_(bool UNUSED(can_assign)) { emit_byte(OP_TRUE); }
static void false_(bool UNUSED(can_assign)) { emit_byte(OP_FALSE); }
static void number(bool UNUSED(can_assign)) { emit_constant(VALUE_NUMBER(strtod(p.previous.start, NULL))); }
static void string(bool UNUSED(can_assign)) {
    emit_constant(VALUE_OBJECT(copy_string(p.previous.start + 1, p.previous.length - 2)));
}

static void variable(bool can_assign) { named_var(p.previous, can_assign); }

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

static void begin_scope(void) { c->scope_depth++; }

static void end_scope(void) {
    while (c->local_count > 0 && c->locals[c->local_count - 1].depth >= c->scope_depth) {
        emit_byte(OP_POP);
        c->local_count--;
    }
    c->scope_depth--;
}

static void add_local(Token name) {
    if (c->local_count > UINT8_MAX) {
        ERROR_PREV("Too many local variables in one scope.");
        return;
    }

    Local *local = &c->locals[c->local_count++];
    local->name = name;
    local->depth = DEPTH_UNINITIALIZED;
}

static void declare_local(void) {
    Token name = p.previous;
    for (int i = c->local_count - 1; i >= 0; i--) {
        Local *local = &c->locals[i];
        if (local->depth < c->scope_depth) break;

        if (token_equals(local->name, name)) {
            ERROR_PREV("Redefinition of a local variable '%.*s'", name.length, name.start);
        }
    }

    add_local(name);
}

static uint8_t parse_var(const char *error_message) {
    expect(TOKEN_IDENTIFIER, error_message);

    if (c->scope_depth == 0) {
        // Global variables are looked up by name, so we save their name as a constant.
        return identifier_constant(p.previous);
    } else {
        // Local variables do not need that, return a dummy value.
        declare_local();
        return 0;
    }
}

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

static void block(void) {
    advance();
    while (!is_next(TOKEN_EOF) && !is_next(TOKEN_RIGHT_BRACE)) declaration();
    expect(TOKEN_RIGHT_BRACE, "Unclosed '{', expected '}' at the end of the block");
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
        case TOKEN_LEFT_BRACE:
            begin_scope();
            block();
            end_scope();
            break;
        case TOKEN_PRINT: print_stmt(); break;
        default:          expression_stmt(); break;
    }
}

static void var_decl(void) {
    advance();
    uint8_t global = parse_var("Expected variable name after 'var'");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        // Value is implicitly nil.
        emit_byte(OP_NIL);
    }
    expect(TOKEN_SEMICOLON, "Expected ';' after variable declaration");

    if (c->scope_depth == 0) {
        emit_byte2(OP_DEFINE_GLOBAL, global);
    } else {
        assert(c->locals[c->local_count - 1].depth == DEPTH_UNINITIALIZED);
        // Local var is already declared, now we define it (mark as initialized).
        c->locals[c->local_count - 1].depth = c->scope_depth;
    }
}

static void declaration(void) {
    switch (p.current.type) {
        case TOKEN_VAR: var_decl(); break;
        default:        statement(); break;
    }
    if (p.is_panicking) synchronize();
}

static void init_parser(Chunk *chunk) { p.chunk = chunk; }

bool compile(const char *source, Chunk *chunk) {
    Compiler compiler = {0};
    c = &compiler;

    init_lexer(source);
    init_parser(chunk);

    advance();
    while (!match(TOKEN_EOF)) declaration();
    emit_byte(OP_RETURN);

#ifdef DEBUG_PRINT_BYTECODE
    if (!p.had_error) disassemble_chunk(current_chunk());
#endif

    return !p.had_error;
}
