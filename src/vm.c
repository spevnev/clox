#include "vm.h"
#include <assert.h>
#include "common.h"
#include "debug.h"
#include "error.h"
#include "value.h"

static void stack_push(VM* vm, Value value) {
    assert(vm->stack_top - vm->stack < STACK_SIZE && "Stack overflow");
    *(vm->stack_top++) = value;
}

static Value stack_pop(VM* vm) {
    assert(vm->stack_top > vm->stack && "Stack underflow");
    return *(--vm->stack_top);
}

static InterpretResult run(VM* vm) {
    // Read macros
#define READ_BYTE() (*vm->ip++)
#define READ_CONST() (vm->chunk->constants.values[READ_BYTE()])
    // Op macros
#define BINARY_OP(op)             \
    do {                          \
        double b = stack_pop(vm); \
        double a = stack_pop(vm); \
        stack_push(vm, a op b);   \
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
            case OP_CONSTANT: stack_push(vm, READ_CONST()); break;
            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE:   BINARY_OP(/); break;
            case OP_NEGATE:   *(vm->stack_top - 1) *= -1; break;
            case OP_RETURN:
                print_value(stack_pop(vm));
                printf("\n");
                return RESULT_OK;
        }
    }

#undef READ_BYTE
#undef READ_CONST
#undef BINARY_OP
}

void init_vm(VM* vm) { vm->stack_top = vm->stack; }

void free_vm(VM* vm) { (void) vm; }

InterpretResult interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    return run(vm);
}
