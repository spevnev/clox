#include "debug.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "chunk.h"
#include "common.h"
#include "object.h"
#include "value.h"

uint32_t disassemble_instr(const Chunk *chunk, uint32_t offset) {
#define READ_U8() (chunk->code[offset++])
#define READ_U16() (offset += 2, (uint16_t) (chunk->code[offset - 2] | (chunk->code[offset - 1] << 8)))

#define INSTR(instr) printf(instr "\n")
#define U8_INSTR(instr) printf(instr " %u\n", READ_U8());
#define JUMP_INSTR(instr)                                                 \
    do {                                                                  \
        uint16_t jump_offset = READ_U16();                                \
        printf(instr " %u -> %04u\n", jump_offset, offset + jump_offset); \
    } while (0)
#define CONST_INSTR(instr)                                                                           \
    do {                                                                                             \
        uint8_t constant = READ_U8();                                                                \
        printf(instr " %u '%s'\n", constant, value_to_temp_cstr(chunk->constants.values[constant])); \
    } while (0)
#define INVOKE_INSTR(instr)                                                                                      \
    do {                                                                                                         \
        uint8_t constant = READ_U8();                                                                            \
        uint8_t arg_num = READ_U8();                                                                             \
        printf(instr " %u '%s' %u\n", constant, value_to_temp_cstr(chunk->constants.values[constant]), arg_num); \
    } while (0)

    uint32_t line = chunk->locs[offset].line;
    if (offset > 0 && chunk->locs[offset - 1].line == line) {
        printf("     | ");
    } else {
        printf("%4d | ", line);
    }

    printf("%04d    ", offset);

    OpCode opcode = READ_U8();
    switch (opcode) {
        case OP_NIL:           INSTR("nil"); break;
        case OP_TRUE:          INSTR("true"); break;
        case OP_FALSE:         INSTR("false"); break;
        case OP_CONSTANT:      CONST_INSTR("const"); break;
        case OP_DUP:           INSTR("dup"); break;
        case OP_POP:           INSTR("pop"); break;
        case OP_POPN:          U8_INSTR("popn"); break;
        case OP_EQUAL:         INSTR("equal"); break;
        case OP_GREATER:       INSTR("greater"); break;
        case OP_LESS:          INSTR("less"); break;
        case OP_ADD:           INSTR("add"); break;
        case OP_SUBTRACT:      INSTR("subtract"); break;
        case OP_MULTIPLY:      INSTR("multiply"); break;
        case OP_DIVIDE:        INSTR("divide"); break;
        case OP_NOT:           INSTR("not"); break;
        case OP_NEGATE:        INSTR("negate"); break;
        case OP_INCR:          INSTR("incr"); break;
        case OP_DECR:          INSTR("decr"); break;
        case OP_DEFINE_GLOBAL: CONST_INSTR("define global"); break;
        case OP_GET_GLOBAL:    CONST_INSTR("get global"); break;
        case OP_SET_GLOBAL:    CONST_INSTR("set global"); break;
        case OP_GET_LOCAL:     U8_INSTR("get local"); break;
        case OP_SET_LOCAL:     U8_INSTR("set local"); break;
        case OP_GET_UPVALUE:   U8_INSTR("get upvalue"); break;
        case OP_SET_UPVALUE:   U8_INSTR("set upvalue"); break;
        case OP_PRINT:         INSTR("print"); break;
        case OP_CONCAT:        U8_INSTR("concat"); break;
        case OP_JUMP:          JUMP_INSTR("jump"); break;
        case OP_JUMP_IF_FALSE: JUMP_INSTR("jump if false"); break;
        case OP_JUMP_IF_TRUE:  JUMP_INSTR("jump if true"); break;
        case OP_LOOP:          {
            uint16_t loop_offset = READ_U16();
            printf("loop %u -> %04u\n", loop_offset, offset - loop_offset);
        } break;
        case OP_CALL:    U8_INSTR("call"); break;
        case OP_CLOSURE: {
            uint8_t constant = chunk->code[offset];
            CONST_INSTR("closure");
            ObjFunction *function = (ObjFunction *) chunk->constants.values[constant].as.object;
            for (uint32_t i = 0; i < function->upvalues_count; i++) {
                uint8_t is_local = READ_U8();
                uint8_t index = READ_U8();
                printf("     |         |  %s %u\n", is_local ? "local" : "upvalue", index);
            }
        } break;
        case OP_CLOSE_UPVALUE: INSTR("close upvalue"); break;
        case OP_RETURN:        INSTR("return"); break;
        case OP_CLASS:         CONST_INSTR("class"); break;
        case OP_METHOD:        CONST_INSTR("method"); break;
        case OP_INHERIT:       INSTR("inherit"); break;
        case OP_GET_FIELD:     CONST_INSTR("get field"); break;
        case OP_SET_FIELD:     CONST_INSTR("set field"); break;
        case OP_INVOKE:        {
            INVOKE_INSTR("invoke");
#ifdef INLINE_CACHING
            offset += sizeof(cache_id_t) + sizeof(void *);
#endif
        } break;
        case OP_GET_SUPER:    CONST_INSTR("get super"); break;
        case OP_SUPER_INVOKE: {
            INVOKE_INSTR("super invoke");
#ifdef INLINE_CACHING
            offset += sizeof(void *);
#endif
        } break;
        case OP_YIELD:      INSTR("yield"); break;
        case OP_AWAIT:      INSTR("await"); break;
        case OP_ARRAY:      U8_INSTR("array"); break;
        case OP_ARRAY_GET:  INSTR("array get"); break;
        case OP_ARRAY_SET:  INSTR("array set"); break;
        case OP_ARRAY_INCR: INSTR("array incr"); break;
        case OP_ARRAY_DECR: INSTR("array decr"); break;
        default:            printf("unknown opcode %d\n", opcode); break;
    }
    return offset;

#undef READ_U8
#undef READ_U16
#undef INSTR
#undef U8_INSTR
#undef JUMP_INSTR
#undef CONST_INSTR
#undef INVOKE_INSTR
}

static const char *HEADER = "line | offset  instruction";
static const int HEADER_LEN = 40;

void disassemble_chunk(const Chunk *chunk, const char *name) {
    float padding = (HEADER_LEN - strlen(name) - 2) / 2.0;
    for (int i = 0; i < ceilf(padding); i++) printf("-");
    printf(" %s ", name);
    for (int i = 0; i < floorf(padding); i++) printf("-");
    printf("\n%s\n", HEADER);
    for (int i = 0; i < HEADER_LEN; i++) printf("-");
    printf("\n");

    uint32_t offset = 0;
    while (offset < chunk->length) offset = disassemble_instr(chunk, offset);
}
