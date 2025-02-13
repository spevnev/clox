#include "debug.h"
#include "chunk.h"
#include "common.h"
#include "value.h"

static uint32_t disassemble_instr(const Chunk* chunk, uint32_t offset) {
    uint32_t line = chunk->lines[offset];
    if (offset > 0 && chunk->lines[offset - 1] == line) {
        printf("     | ");
    } else {
        printf("%4d | ", line);
    }

    printf("%04d    ", offset);

    uint8_t opcode = chunk->code[offset++];
    switch (opcode) {
        case OP_CONSTANT: {
            uint32_t const_idx = chunk->code[offset++];
            printf("const %u '", const_idx);
            print_value(chunk->constants.values[const_idx]);
            printf("'\n");
            return offset;
        }
        default:
            printf("unknown opcode %d\n", opcode);
            return offset;
    }
}

void disassemble_chunk(const Chunk* chunk) {
    printf("line | offset  instruction\n");
    printf("--------------------------\n");

    uint32_t offset = 0;
    while (offset < chunk->length) offset = disassemble_instr(chunk, offset);
}
