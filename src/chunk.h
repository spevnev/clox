#ifndef CLOX_CHUNK_H_
#define CLOX_CHUNK_H_

#include "value.h"

typedef enum {
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_CONSTANT,
    OP_DUP,
    OP_POP,
    OP_POPN,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_INCR,
    OP_DECR,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_PRINT,
    OP_CONCAT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,
    OP_CALL,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_METHOD,
    OP_INHERIT,
    OP_GET_FIELD,
    OP_SET_FIELD,
    OP_INVOKE,
    OP_GET_SUPER,
    OP_SUPER_INVOKE,
    OP_YIELD,
    OP_AWAIT,
    OP_ARRAY,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_ARRAY_INCR,
    OP_ARRAY_DECR
} OpCode;

typedef struct {
    uint32_t line;
    uint32_t column;
} Loc;

typedef struct {
    uint32_t length;
    uint32_t capacity;
    uint8_t *code;
    Loc *locs;
    ValueVec constants;
} Chunk;

#define MAX_OPERAND UINT8_MAX

void free_chunk(Chunk *chunk);
void push_byte(Chunk *chunk, uint8_t byte, Loc loc);
void push_byte_n(Chunk *chunk, uint8_t byte, uint32_t count, Loc loc);
uint32_t push_constant(Chunk *chunk, Value value);

#endif  // CLOX_CHUNK_H_
