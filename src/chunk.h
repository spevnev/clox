#ifndef CLOX_CHUNK_H_
#define CLOX_CHUNK_H_

#include "common.h"
#include "value.h"

typedef enum { OP_CONSTANT, OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_NEGATE, OP_RETURN } OpCode;

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
