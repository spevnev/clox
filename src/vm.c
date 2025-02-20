#include "vm.h"
#include <assert.h>
#include <stdarg.h>
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "error.h"
#include "value.h"

#define RUNTIME_ERROR(vm, ...) ERROR_AT(vm->chunk->lines[vm->ip - vm->chunk->code - 1], __VA_ARGS__)

static void stack_push(VM* vm, Value value) {
    assert(vm->stack_top - vm->stack < STACK_SIZE && "Stack overflow");
    *(vm->stack_top++) = value;
}

static Value stack_pop(VM* vm) {
    assert(vm->stack_top > vm->stack && "Stack underflow");
    return *(--vm->stack_top);
}

static Value stack_peek(VM* vm, int distance) {
    assert(distance >= 0 && "Peek distance must be non-negative");
    return *(vm->stack_top - 1 - distance);
}

static InterpretResult run(VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONST() (vm->chunk->constants.values[READ_BYTE()])

#define BINARY_OP(value_type, op)                                                           \
    do {                                                                                    \
        if (stack_peek(vm, 0).type != VAL_NUMBER || stack_peek(vm, 1).type != VAL_NUMBER) { \
            RUNTIME_ERROR(vm, "Operands must be numbers");                                  \
            return RESULT_RUNTIME_ERROR;                                                    \
        }                                                                                   \
        double b = stack_pop(vm).as.number;                                                 \
        double a = stack_pop(vm).as.number;                                                 \
        stack_push(vm, value_type(a op b));                                                 \
    } while (0)

    for (;;) {
#ifdef DEBUG_TRACE_STACK
        print_stack(vm);
#endif
#ifdef DEBUG_TRACE_INSTR
        disassemble_instr(vm->chunk, vm->ip - vm->chunk->code);
#endif

        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_NIL:      stack_push(vm, VALUE_NIL()); break;
            case OP_TRUE:     stack_push(vm, VALUE_BOOL(true)); break;
            case OP_FALSE:    stack_push(vm, VALUE_BOOL(false)); break;
            case OP_CONSTANT: stack_push(vm, READ_CONST()); break;
            case OP_EQUAL:    stack_push(vm, VALUE_BOOL(value_equals(stack_pop(vm), stack_pop(vm)))); break;
            case OP_GREATER:  BINARY_OP(VALUE_BOOL, >); break;
            case OP_LESS:     BINARY_OP(VALUE_BOOL, <); break;
            case OP_ADD:      BINARY_OP(VALUE_NUMBER, +); break;
            case OP_SUBTRACT: BINARY_OP(VALUE_NUMBER, -); break;
            case OP_MULTIPLY: BINARY_OP(VALUE_NUMBER, *); break;
            case OP_DIVIDE:   BINARY_OP(VALUE_NUMBER, /); break;
            case OP_NOT:      stack_push(vm, VALUE_BOOL(!value_is_truthy(stack_pop(vm)))); break;
            case OP_NEGATE:
                if (stack_peek(vm, 0).type != VAL_NUMBER) {
                    RUNTIME_ERROR(vm, "Operand must be a number");
                    return RESULT_RUNTIME_ERROR;
                }
                (vm->stack_top - 1)->as.number *= -1;
                break;
            case OP_RETURN:
                print_value(stack_pop(vm));
                printf("\n");
                return RESULT_OK;
            default: UNREACHABLE();
        }
    }

#undef READ_BYTE
#undef READ_CONST
#undef BINARY_OP
}

void init_vm(VM* vm) { vm->stack_top = vm->stack; }

void free_vm(VM* vm) { (void) vm; }

InterpretResult interpret(VM* vm, const char* source) {
    Chunk chunk = {0};
    if (!compile(source, &chunk)) {
        free_chunk(&chunk);
        return RESULT_COMPILE_ERROR;
    }

    vm->chunk = &chunk;
    vm->ip = chunk.code;
    InterpretResult result = run(vm);

    free_chunk(&chunk);
    return result;
}
