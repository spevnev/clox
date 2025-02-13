#ifndef CLOX_CHUNK_H_
#define CLOX_CHUNK_H_

#include "common.h"
#include "value.h"

typedef enum { OP_CONSTANT } OpCode;

typedef struct {
    uint32_t length;
    uint32_t capacity;
    uint8_t *code;
    uint32_t *lines;

    ValueVec constants;
} Chunk;

void chunk_push_byte(Chunk *chunk, uint8_t byte, uint32_t line);
uint32_t chunk_push_constant(Chunk *chunk, Value value);
void chunk_free(Chunk *chunk);

#endif  // CLOX_CHUNK_H_
