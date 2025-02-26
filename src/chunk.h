#ifndef CLOX_CHUNK_H_
#define CLOX_CHUNK_H_

#include "common.h"
#include "value.h"

typedef enum {
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_CONSTANT,
    OP_POP,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,
    OP_RETURN,
} OpCode;

typedef struct {
    uint32_t length;
    uint32_t capacity;
    uint8_t *code;
    uint32_t *lines;

    ValueVec constants;
} Chunk;

void free_chunk(Chunk *chunk);
void push_byte(Chunk *chunk, uint8_t byte, uint32_t line);
uint32_t push_constant(Chunk *chunk, Value value);

#endif  // CLOX_CHUNK_H_
