#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "value.h"

void free_chunk(Chunk *chunk) {
    chunk->constants.values = ARRAY_FREE(chunk->constants.values, chunk->constants.capacity);
    chunk->constants.capacity = 0;
    chunk->constants.length = 0;

    chunk->code = ARRAY_FREE(chunk->code, chunk->capacity);
    chunk->lines = ARRAY_FREE(chunk->lines, chunk->capacity);
    chunk->capacity = 0;
    chunk->length = 0;
}

void push_byte(Chunk *chunk, uint8_t byte, uint32_t line) {
    if (chunk->length >= chunk->capacity) {
        uint32_t old_capacity = chunk->capacity;
        chunk->capacity = VEC_GROW_CAPACITY(chunk->capacity);
        chunk->code = ARRAY_REALLOC(chunk->code, old_capacity, chunk->capacity);
        chunk->lines = ARRAY_REALLOC(chunk->lines, old_capacity, chunk->capacity);
    }

    chunk->code[chunk->length] = byte;
    chunk->lines[chunk->length] = line;
    chunk->length++;
}

uint32_t push_constant(Chunk *chunk, Value value) {
    values_push(&chunk->constants, value);
    return chunk->constants.length - 1;
}
