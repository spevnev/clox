#include "debug.h"
#include "chunk.h"
#include "common.h"
#include "value.h"

uint32_t disassemble_instr(const Chunk* chunk, uint32_t offset) {
#define INSTR(instr) printf(instr "\n")
#define CONST_INSTR(instr)                               \
    do {                                                 \
        uint32_t const_idx = chunk->code[offset++];      \
        printf(instr " %u '", const_idx);                \
        print_value(chunk->constants.values[const_idx]); \
        printf("'\n");                                   \
    } while (0)

    uint32_t line = chunk->lines[offset];
    if (offset > 0 && chunk->lines[offset - 1] == line) {
        printf("     | ");
    } else {
        printf("%4d | ", line);
    }

    printf("%04d    ", offset);

    OpCode opcode = chunk->code[offset++];
    switch (opcode) {
        case OP_NIL:           INSTR("nil"); break;
        case OP_TRUE:          INSTR("true"); break;
        case OP_FALSE:         INSTR("false"); break;
        case OP_CONSTANT:      CONST_INSTR("const"); break;
        case OP_POP:           INSTR("pop"); break;
        case OP_EQUAL:         INSTR("equal"); break;
        case OP_GREATER:       INSTR("greater"); break;
        case OP_LESS:          INSTR("less"); break;
        case OP_ADD:           INSTR("add"); break;
        case OP_SUBTRACT:      INSTR("subtract"); break;
        case OP_MULTIPLY:      INSTR("multiply"); break;
        case OP_DIVIDE:        INSTR("divide"); break;
        case OP_NOT:           INSTR("not"); break;
        case OP_NEGATE:        INSTR("negate"); break;
        case OP_PRINT:         INSTR("print"); break;
        case OP_RETURN:        INSTR("return"); break;
        default:               printf("unknown opcode %d\n", opcode); break;
    }
    return offset;

#undef INSTR
#undef CONST_INSTR
}

void disassemble_chunk(const Chunk* chunk) {
    printf("line | offset  instruction\n");
    printf("--------------------------\n");

    uint32_t offset = 0;
    while (offset < chunk->length) offset = disassemble_instr(chunk, offset);
}
