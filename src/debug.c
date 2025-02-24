#include "debug.h"
#include "chunk.h"
#include "common.h"
#include "value.h"

uint32_t disassemble_instr(const Chunk* chunk, uint32_t offset) {
    uint32_t line = chunk->lines[offset];
    if (offset > 0 && chunk->lines[offset - 1] == line) {
        printf("     | ");
    } else {
        printf("%4d | ", line);
    }

    printf("%04d    ", offset);

    OpCode opcode = chunk->code[offset++];
    switch (opcode) {
        case OP_NIL:      printf("nil\n"); break;
        case OP_TRUE:     printf("true\n"); break;
        case OP_FALSE:    printf("false\n"); break;
        case OP_CONSTANT: {
            uint32_t const_idx = chunk->code[offset++];
            printf("const %u '", const_idx);
            print_value(chunk->constants.values[const_idx]);
            printf("'\n");
            break;
        }
        case OP_EQUAL:    printf("equal\n"); break;
        case OP_GREATER:  printf("greater\n"); break;
        case OP_LESS:     printf("less\n"); break;
        case OP_ADD:      printf("add\n"); break;
        case OP_SUBTRACT: printf("subtract\n"); break;
        case OP_MULTIPLY: printf("multiply\n"); break;
        case OP_DIVIDE:   printf("divide\n"); break;
        case OP_NOT:      printf("not\n"); break;
        case OP_NEGATE:   printf("negate\n"); break;
        case OP_RETURN:   printf("return\n"); break;
        default:          printf("unknown opcode %d\n", opcode); break;
    }
    return offset;
}

void disassemble_chunk(const Chunk* chunk) {
    printf("line | offset  instruction\n");
    printf("--------------------------\n");

    uint32_t offset = 0;
    while (offset < chunk->length) offset = disassemble_instr(chunk, offset);
}
