#include "compiler.h"
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "lexer.h"
#include "memory.h"
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
    bool is_captured;
} Local;

#define DEPTH_UNINITIALIZED -1

typedef enum {
    FUN_SCRIPT,
    FUN_FUNCTION,
    FUN_METHOD,
    FUN_INITIALIZER,
} FunctionType;

typedef struct {
    bool is_local;
    uint8_t index;
} Upvalue;

typedef struct Compiler {
    struct Compiler *enclosing;
    FunctionType function_type;
    ObjFunction *function;
    int scope_depth;
    uint32_t locals_count;
    Local locals[LOCALS_SIZE];
    Upvalue upvalues[UPVALUES_SIZE];
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    bool has_superclass;
} ClassCompiler;

// Name of top-level function
#define SCRIPT_NAME "<script>"

static const Token this_token = {.start = "this", .length = 4};
static const Token super_token = {.start = "super", .length = 5};

static Parser p = {0};
static Compiler *c = NULL;
static ClassCompiler *cc = NULL;

static const ParseRule *get_rule(TokenType op);
static void statement(void);
static void declaration(void);
static void var_decl(void);

// Most errors do not require synchronization, so `is_panicking` should be set by the caller.
static void error_at(Loc loc, const char *fmt, ...) {
    if (p.is_panicking) return;
    p.had_error = true;

    va_list args;
    va_start(args, fmt);
    error_varg(loc, fmt, args);
    va_end(args);
}
#define error_prev(...) error_at(p.previous.loc, __VA_ARGS__)
#define error_current(...) error_at(p.current.loc, __VA_ARGS__)

static bool is_next(TokenType type) { return p.current.type == type; }

static void advance(void) {
    p.previous = p.current;
    for (;;) {
        p.current = next_token();
        if (p.current.type != TOKEN_ERROR) break;
        error_current(p.current.start);
        p.is_panicking = true;
    }
}

static void expect(TokenType type, const char *error_message) {
    if (p.current.type == type) {
        advance();
    } else {
        error_current(error_message);
        p.is_panicking = true;
    }
}

static bool match(TokenType type) {
    if (p.current.type != type) return false;
    advance();
    return true;
}

static Chunk *current_chunk(void) { return &c->function->chunk; }

static uint8_t add_constant(Value constant) {
    stack_push(constant);
    uint32_t index = push_constant(current_chunk(), constant);
    stack_pop();

    if (index >= CONSTANTS_SIZE) {
        error_prev("Too many constants in one chunk");
        return 0;
    }

    return index;
}

static uint8_t identifier_constant(Token token) {
    return add_constant(VALUE_OBJECT(copy_string(token.start, token.length)));
}

static void emit_byte(uint8_t byte) { push_byte(current_chunk(), byte, p.previous.loc); }
static void emit_byte2(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}
static void emit_byte3(uint8_t byte1, uint8_t byte2, uint8_t byte3) {
    emit_byte(byte1);
    emit_byte(byte2);
    emit_byte(byte3);
}
#ifdef INLINE_CACHING
static void emit_byte_n(uint8_t byte, uint32_t count) { push_byte_n(current_chunk(), byte, count, p.previous.loc); }
#endif

static void emit_constant(Value constant) { emit_byte2(OP_CONSTANT, add_constant(constant)); }

static void emit_pop(uint8_t n) {
    if (n == 1) {
        emit_byte(OP_POP);
    } else {
        emit_byte2(OP_POPN, n);
    }
}

// Emits a jump instruction with placeholder operand and returns the offset to backpatch it later.
static uint32_t emit_jump(uint8_t jump_op) {
    emit_byte(jump_op);
    uint32_t operand_offset = current_chunk()->length;
    emit_byte2(0xFF, 0xFF);
    return operand_offset;
}

static void patch_jump(uint32_t offset) {
    // -2 adjusts for the 16-bit jump operand that is already skipped.
    uint32_t jump = current_chunk()->length - offset - 2;
    if (jump > UINT16_MAX) error_at(current_chunk()->locs[offset], "Jump target is too far");
    memcpy(current_chunk()->code + offset, &jump, sizeof(uint16_t));
}

// Emits a loop (jump back) instruction that goes back to `loop_start`.
static void emit_loop(uint32_t loop_start) {
    emit_byte(OP_LOOP);
    // 2 adjusts for the loop instruction and its operand.
    uint32_t offset = current_chunk()->length + 2 - loop_start;
    if (offset > UINT16_MAX) error_at(current_chunk()->locs[loop_start], "Loop body is too big");
    emit_byte2(offset & 0xFF, (offset >> 8) & 0xFF);
}

// Implicit return, emits `nil` for functions, and instance (stored in the reserved slot) for `init` method.
static void emit_return(void) {
    if (c->function_type == FUN_INITIALIZER) {
        emit_byte2(OP_GET_LOCAL, 0);
    } else {
        emit_byte(OP_NIL);
    }
    emit_byte(OP_RETURN);
}

static void parse_precedence(Precedence precedence) {
    advance();

    ParseFn prefix_rule = get_rule(p.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error_prev("Expected an expression but found '%.*s'", p.previous.length, p.previous.start);
        p.is_panicking = true;
        return;
    }

    Loc loc = p.previous.loc;
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(p.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(p.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        error_at(loc, "Invalid assignment target");
        p.is_panicking = true;
    }
    if (match(TOKEN_PLUS_PLUS)) error_prev("Invalid post increment target");
    if (match(TOKEN_MINUS_MINUS)) error_prev("Invalid post decrement target");
}

static void expression(void) { parse_precedence(PREC_ASSIGNMENT); }

static bool token_equals(Token a, Token b) { return a.length == b.length && memcmp(a.start, b.start, a.length) == 0; }

static uint8_t add_upvalue(Compiler *c, uint8_t index, bool is_local) {
    for (uint32_t i = 0; i < c->function->upvalues_count; i++) {
        Upvalue *upvalue = &c->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local) return i;
    }

    uint32_t upvalue_index = c->function->upvalues_count++;
    if (upvalue_index >= UPVALUES_SIZE) {
        error_prev("Function captures too many variables.");
        return 0;
    }

    c->upvalues[upvalue_index].is_local = is_local;
    c->upvalues[upvalue_index].index = index;

    return upvalue_index;
}

static int resolve_local(Compiler *c, Token name) {
    for (int i = c->locals_count - 1; i >= 0; i--) {
        if (!token_equals(c->locals[i].name, name)) continue;
        if (c->locals[i].depth == DEPTH_UNINITIALIZED) {
            error_prev("Cannot read local variable '%.*s' in its own initializer", name.length, name.start);
        }
        return i;
    }
    return -1;
}

static int resolve_upvalue(Compiler *c, Token name) {
    if (c->enclosing == NULL) return -1;

    int index;
    if ((index = resolve_local(c->enclosing, name)) != -1) {
        c->enclosing->locals[index].is_captured = true;
        return add_upvalue(c, index, true);
    }
    if ((index = resolve_upvalue(c->enclosing, name)) != -1) return add_upvalue(c, index, false);
    return -1;
}

static void named_var(Token token, bool can_assign) {
    int index;
    uint8_t get_op, set_op, operand;
    if ((index = resolve_local(c, token)) != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
        operand = index;
    } else if ((index = resolve_upvalue(c, token)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
        operand = index;
    } else {
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
        operand = identifier_constant(token);
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_byte2(set_op, operand);
    } else if (match(TOKEN_PLUS_PLUS)) {
        emit_byte2(get_op, operand);
        emit_byte(OP_INCR);
        emit_byte2(set_op, operand);
        emit_byte(OP_DECR);
    } else if (match(TOKEN_MINUS_MINUS)) {
        emit_byte2(get_op, operand);
        emit_byte(OP_DECR);
        emit_byte2(set_op, operand);
        emit_byte(OP_INCR);
    } else {
        emit_byte2(get_op, operand);
    }
}

static uint8_t args(void) {
    uint8_t arg_num = 0;
    if (!is_next(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (arg_num == MAX_ARITY) {
                error_prev("Function call has too many arguments (max is %d)", MAX_ARITY);
                p.is_panicking = true;
            }
            arg_num++;
        } while (match(TOKEN_COMMA));
    }
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after arguments");
    return arg_num;
}

static void nil(UNUSED(bool can_assign)) { emit_byte(OP_NIL); }
static void true_(UNUSED(bool can_assign)) { emit_byte(OP_TRUE); }
static void false_(UNUSED(bool can_assign)) { emit_byte(OP_FALSE); }
static void number(UNUSED(bool can_assign)) { emit_constant(VALUE_NUMBER(strtod(p.previous.start, NULL))); }
static void string(UNUSED(bool can_assign)) {
    emit_constant(VALUE_OBJECT(copy_string(p.previous.start + 1, p.previous.length - 2)));
}

static void variable(bool can_assign) { named_var(p.previous, can_assign); }

static void this(UNUSED(bool can_assign)) {
    if (cc == NULL) error_prev("Cannot use 'this' outside of class");

    // Treat `this` as a local variable.
    variable(false);
}

static void super(UNUSED(bool can_assign)) {
    if (cc == NULL) {
        error_prev("Cannot use 'super' outside of class");
    } else if (!cc->has_superclass) {
        error_prev("Cannot use 'super' in a class without superclass");
    }

    expect(TOKEN_DOT, "Expected '.' after 'super'");
    expect(TOKEN_IDENTIFIER, "Expected superclass method name after 'super'");
    uint8_t name = identifier_constant(p.previous);

    named_var(this_token, false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t arg_num = args();
        named_var(super_token, false);
        emit_byte3(OP_SUPER_INVOKE, name, arg_num);
#ifdef INLINE_CACHING
        // Zero-initialize inline cache.
        emit_byte_n(0, sizeof(void *));
#endif
    } else {
        named_var(super_token, false);
        emit_byte2(OP_GET_SUPER, name);
    }
}

static void grouping(UNUSED(bool can_assign)) {
    expression();
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after expression");
}

static void unary(UNUSED(bool can_assign)) {
    TokenType op = p.previous.type;

    parse_precedence(PREC_UNARY);

    switch (op) {
        case TOKEN_BANG:        emit_byte(OP_NOT); break;
        case TOKEN_MINUS:       emit_byte(OP_NEGATE); break;
        case TOKEN_MINUS_MINUS: emit_byte2(OP_NEGATE, OP_NEGATE); break;
        default:                UNREACHABLE();
    }
}

static void binary(UNUSED(bool can_assign)) {
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

static void and_(UNUSED(bool can_assign)) {
    // Jump over second operand if the first one is false (short-circuiting).
    uint32_t jump = emit_jump(OP_JUMP_IF_FALSE);

    // If we fall through the jump, it means that the first operand is true.
    // Result now depends only on the second one so we pop the first operand.
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);

    patch_jump(jump);
}

static void or_(UNUSED(bool can_assign)) {
    // Same logic as in `and_` but reversed.
    uint32_t jump = emit_jump(OP_JUMP_IF_TRUE);
    emit_byte(OP_POP);
    parse_precedence(PREC_OR);
    patch_jump(jump);
}

static void conditional(UNUSED(bool can_assign)) {
    uint32_t jump_over_then = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_ASSIGNMENT);
    uint32_t jump_over_else = emit_jump(OP_JUMP);

    expect(TOKEN_COLON, "Expected ':' after then branch of conditional (ternary) operator");
    patch_jump(jump_over_then);
    emit_byte(OP_POP);
    parse_precedence(PREC_CONDITIONAL);
    patch_jump(jump_over_else);
}

static void call(UNUSED(bool can_assign)) { emit_byte2(OP_CALL, args()); }

static void dot(bool can_assign) {
    expect(TOKEN_IDENTIFIER, "Expected field after '.'");
    uint8_t name = identifier_constant(p.previous);

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_byte2(OP_SET_FIELD, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t arg_num = args();
        emit_byte3(OP_INVOKE, name, arg_num);
#ifdef INLINE_CACHING
        // Zero-initialize inline cache.
        emit_byte_n(0, sizeof(cache_id_t) + sizeof(void *));
#endif
    } else if (match(TOKEN_PLUS_PLUS)) {
        // Get/set field consumes an instance from the stack, so we have to duplicate it for the second call.
        emit_byte(OP_DUP);
        emit_byte2(OP_GET_FIELD, name);
        emit_byte(OP_INCR);
        emit_byte2(OP_SET_FIELD, name);
        emit_byte(OP_DECR);
    } else if (match(TOKEN_MINUS_MINUS)) {
        emit_byte(OP_DUP);
        emit_byte2(OP_GET_FIELD, name);
        emit_byte(OP_DECR);
        emit_byte2(OP_SET_FIELD, name);
        emit_byte(OP_INCR);
    } else {
        emit_byte2(OP_GET_FIELD, name);
    }
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
    [TOKEN_THIS]          = { this,     NULL,        PREC_NONE        },
    [TOKEN_SUPER]         = { super,    NULL,        PREC_NONE        },
    [TOKEN_BANG]          = { unary,    NULL,        PREC_NONE        },
    [TOKEN_MINUS_MINUS]   = { unary,    NULL,        PREC_NONE        },
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
    [TOKEN_AND]           = { NULL,     and_,        PREC_AND         },
    [TOKEN_OR]            = { NULL,     or_,         PREC_OR          },
    [TOKEN_QUESTION]      = { NULL,     conditional, PREC_CONDITIONAL },
    [TOKEN_LEFT_PAREN]    = { grouping, call,        PREC_CALL        },
    [TOKEN_DOT]           = { NULL,     dot,         PREC_CALL        },
    // clang-format on
};

static const ParseRule *get_rule(TokenType op) { return &rules[op]; }

static void init_compiler(Compiler *compiler, FunctionType function_type, ObjString *name) {
    compiler->enclosing = c;
    compiler->function_type = function_type;

    stack_push(VALUE_OBJECT(name));
    compiler->function = new_function(name);
    stack_pop();

    // First slot is reserved for instance (this) in methods, or closure in functions.
    compiler->locals_count = 1;
    if (function_type == FUN_METHOD || function_type == FUN_INITIALIZER) {
        compiler->locals[0] = (Local) {
            .name = this_token,
            .depth = 0,
            .is_captured = false,
        };
    }

    c = compiler;
}

static ObjFunction *end_compiler(void) {
    emit_return();

#ifdef DEBUG_PRINT_BYTECODE
    if (!p.had_error) disassemble_chunk(current_chunk(), c->function->name->cstr);
#endif

    ObjFunction *function = c->function;
    c = c->enclosing;
    return function;
}

static void begin_scope(void) { c->scope_depth++; }

static void end_scope(void) {
    uint8_t pop_count = 0;

    c->scope_depth--;
    for (; c->locals_count > 0 && c->locals[c->locals_count - 1].depth > c->scope_depth; c->locals_count--) {
        if (!c->locals[c->locals_count - 1].is_captured) {
            pop_count++;
            continue;
        }

        if (pop_count > 0) {
            emit_pop(pop_count);
            pop_count = 0;
        }

        emit_byte(OP_CLOSE_UPVALUE);
    }
    if (pop_count > 0) emit_pop(pop_count);
}

static void add_local(Token name) {
    if (c->locals_count >= LOCALS_SIZE) {
        error_prev("Too many local variables in one scope");
        return;
    }

    Local *local = &c->locals[c->locals_count++];
    local->name = name;
    local->depth = DEPTH_UNINITIALIZED;
    local->is_captured = false;
}

static void declare_local(void) {
    Token name = p.previous;
    for (int i = c->locals_count - 1; i >= 0; i--) {
        Local *local = &c->locals[i];
        if (local->depth < c->scope_depth) break;

        if (token_equals(local->name, name)) {
            error_prev("Redeclaration of local variable '%.*s'", name.length, name.start);
        }
    }

    add_local(name);
}

static void mark_initialized(void) {
    if (c->scope_depth == 0) return;  // Global variables are always initialized.
    c->locals[c->locals_count - 1].depth = c->scope_depth;
}

static uint8_t declare_var(void) {
    if (c->scope_depth == 0) {
        // Global variables are looked up by name, so we save their name as a constant.
        return identifier_constant(p.previous);
    } else {
        // Local variables do not need that, return a dummy value.
        declare_local();
        return 0;
    }
}

static void define_var(uint8_t global) {
    if (c->scope_depth == 0) {
        emit_byte2(OP_DEFINE_GLOBAL, global);
    } else {
        mark_initialized();
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
            case TOKEN_RETURN:
            case TOKEN_LEFT_BRACE: return;
            default:               advance(); break;
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
    expect(TOKEN_SEMICOLON, "Expected ';' after expression");
    emit_byte(OP_POP);
}

static void print_stmt(void) {
    advance();
    expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after print statement");
    emit_byte(OP_PRINT);
}

static void if_stmt(void) {
    advance();
    expect(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");
    expression();
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after condition");

    uint32_t jump_over_then = emit_jump(OP_JUMP_IF_FALSE);

    emit_byte(OP_POP);
    statement();
    uint32_t jump_over_else = emit_jump(OP_JUMP);

    patch_jump(jump_over_then);
    emit_byte(OP_POP);
    if (match(TOKEN_ELSE)) statement();

    patch_jump(jump_over_else);
}

static void while_stmt(void) {
    uint32_t loop_start = current_chunk()->length;

    advance();
    expect(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
    expression();
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after condition");

    uint32_t exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);

    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void for_stmt(void) {
    begin_scope();

    advance();
    expect(TOKEN_LEFT_PAREN, "Expected '(' after 'for'");

    if (is_next(TOKEN_VAR)) {
        var_decl();
    } else if (!match(TOKEN_SEMICOLON)) {
        expression();
        emit_byte(OP_POP);
        expect(TOKEN_SEMICOLON, "Expected ';' after initializer clause of 'for'");
    }

    uint32_t loop_start = current_chunk()->length;
    uint32_t exit_jump = 0;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        expect(TOKEN_SEMICOLON, "Expected ';' after condition clause of 'for'");

        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        // If the loop has update clause, then we skip it after condition clause
        // using body_jump and then return by changing loop_start to point here.
        uint32_t body_jump = emit_jump(OP_JUMP);

        uint32_t update_start = current_chunk()->length;
        expression();
        emit_byte(OP_POP);
        expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after for loop's clauses");

        emit_loop(loop_start);
        loop_start = update_start;

        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != 0) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }

    end_scope();
}

static void return_stmt(void) {
    advance();
    if (c->function_type == FUN_SCRIPT) error_prev("Cannot return outside of function");

    if (match(TOKEN_SEMICOLON)) {
        emit_return();
    } else {
        if (c->function_type == FUN_INITIALIZER) error_current("Cannot return value from initializer");

        expression();
        expect(TOKEN_SEMICOLON, "Expected ';' after return");
        emit_byte(OP_RETURN);
    }
}

static void statement(void) {
    switch (p.current.type) {
        case TOKEN_LEFT_BRACE:
            begin_scope();
            block();
            end_scope();
            break;
        case TOKEN_PRINT:  print_stmt(); break;
        case TOKEN_IF:     if_stmt(); break;
        case TOKEN_WHILE:  while_stmt(); break;
        case TOKEN_FOR:    for_stmt(); break;
        case TOKEN_RETURN: return_stmt(); break;
        default:           expression_stmt(); break;
    }
}

static void var_decl(void) {
    advance();
    expect(TOKEN_IDENTIFIER, "Expected a variable name after 'var'");

    uint8_t global = declare_var();

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        // No initializer, value is implicitly nil.
        emit_byte(OP_NIL);
    }
    expect(TOKEN_SEMICOLON, "Expected ';' after variable declaration");

    define_var(global);
}

static void function(FunctionType type) {
    Compiler compiler = {0};
    init_compiler(&compiler, type, copy_string(p.previous.start, p.previous.length));
    begin_scope();

    expect(TOKEN_LEFT_PAREN, "Expected '(' after function name");
    if (!is_next(TOKEN_RIGHT_PAREN)) {
        do {
            if (c->function->arity == MAX_ARITY) {
                error_prev("Function has too many parameters (max is %d)", MAX_ARITY);
                p.is_panicking = true;
            }
            c->function->arity++;
            expect(TOKEN_IDENTIFIER, "Expected parameter name");
            define_var(declare_var());
        } while (match(TOKEN_COMMA));
    }
    expect(TOKEN_RIGHT_PAREN, "Unclosed '(', expected ')' after parameters");

    if (is_next(TOKEN_LEFT_BRACE)) {
        block();
    } else {
        error_prev("Expected '{' before function body");
        p.is_panicking = true;
    }

    end_scope();
    ObjFunction *function = end_compiler();

    emit_byte2(OP_CLOSURE, add_constant(VALUE_OBJECT(function)));
    for (uint32_t i = 0; i < function->upvalues_count; i++) {
        emit_byte2(compiler.upvalues[i].is_local ? 1 : 0, compiler.upvalues[i].index);
    }
}

static void fun_decl(void) {
    advance();
    expect(TOKEN_IDENTIFIER, "Expected function name after 'fun'");

    uint8_t global = declare_var();
    mark_initialized();  // Define it right away to allow recursive functions.

    function(FUN_FUNCTION);

    define_var(global);
}

static void class_decl(void) {
    ClassCompiler class_compiler = {.enclosing = cc};
    cc = &class_compiler;

    advance();
    expect(TOKEN_IDENTIFIER, "Expected class name after 'class'");
    Token class_name = p.previous;

    emit_byte2(OP_CLASS, identifier_constant(class_name));
    define_var(declare_var());

    if (match(TOKEN_LESS)) {
        expect(TOKEN_IDENTIFIER, "Expected a superclass name after '<'");
        if (token_equals(p.previous, class_name)) error_prev("Class cannot inherit from itself");

        begin_scope();
        named_var(p.previous, false);
        add_local(super_token);
        mark_initialized();

        // Load the class back onto the stack to be used in OP_INHERIT.
        named_var(class_name, false);
        emit_byte(OP_INHERIT);

        cc->has_superclass = true;
    }

    // Load the class back onto the stack to be used in OP_METHOD.
    named_var(class_name, false);
    expect(TOKEN_LEFT_BRACE, "Expected '{' before class body");
    while (!is_next(TOKEN_EOF) && !is_next(TOKEN_RIGHT_BRACE)) {
        expect(TOKEN_IDENTIFIER, "Expected method name");
        uint8_t name = identifier_constant(p.previous);

        FunctionType fun_type = FUN_METHOD;
        if (p.previous.length == 4 && memcmp(p.previous.start, "init", 4) == 0) fun_type = FUN_INITIALIZER;
        function(fun_type);

        emit_byte2(OP_METHOD, name);
    }
    expect(TOKEN_RIGHT_BRACE, "Unclosed '{', expected '}' after class body");
    emit_byte(OP_POP);  // Pop class.

    if (cc->has_superclass) end_scope();
    cc = class_compiler.enclosing;
}

static void declaration(void) {
    switch (p.current.type) {
        case TOKEN_VAR:   var_decl(); break;
        case TOKEN_FUN:   fun_decl(); break;
        case TOKEN_CLASS: class_decl(); break;
        default:          statement(); break;
    }
    if (p.is_panicking) synchronize();
}

ObjFunction *compile(const char *source) {
    init_lexer(source);

    Compiler compiler = {0};
    init_compiler(&compiler, FUN_SCRIPT, copy_string(SCRIPT_NAME, strlen(SCRIPT_NAME)));

    advance();
    while (!match(TOKEN_EOF)) declaration();

    ObjFunction *script = end_compiler();
    return p.had_error ? NULL : script;
}

void mark_compiler_roots(void) {
    for (Compiler *current = c; current != NULL; current = current->enclosing) {
        mark_object((Object *) current->function);
    }
}
