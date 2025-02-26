#include "vm.h"
#include <assert.h>
#include <stdarg.h>
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "error.h"
#include "value.h"

VM vm = {0};

#define RUNTIME_ERROR(...) ERROR_AT(vm.chunk->lines[vm.ip - vm.chunk->code - 1], __VA_ARGS__)

__attribute__((unused)) static void print_stack(void) {
    printf("Stack: ");
    for (const Value* value = vm.stack; value < vm.stack_top; value++) {
        if (value != vm.stack) printf(", ");
        print_value(*value);
    }
    printf("\n");
}
static void stack_push(Value value) {
    assert(vm.stack_top - vm.stack < STACK_SIZE && "Stack overflow");
    *(vm.stack_top++) = value;
}

static Value stack_pop(void) {
    assert(vm.stack_top > vm.stack && "Stack underflow");
    return *(--vm.stack_top);
}

static Value stack_peek(int distance) {
    assert(distance >= 0 && "Peek distance must be non-negative");
    assert(distance < vm.stack_top - vm.stack && "Peek distance points outside of stack");
    return *(vm.stack_top - 1 - distance);
}

static InterpretResult run(void) {
#define READ_U8() (*vm.ip++)
#define READ_U16() (vm.ip += 2, (uint16_t) (*(vm.ip - 2) | (*(vm.ip - 1) << 8)))
#define READ_CONST() (vm.chunk->constants.values[READ_U8()])
#define READ_STRING() ((ObjString*) READ_CONST().as.object)

#define BINARY_OP(value_type, op)                                                   \
    do {                                                                            \
        if (stack_peek(0).type != VAL_NUMBER || stack_peek(1).type != VAL_NUMBER) { \
            RUNTIME_ERROR("Operands must be numbers");                              \
            return RESULT_RUNTIME_ERROR;                                            \
        }                                                                           \
        double b = stack_pop().as.number;                                           \
        double a = stack_pop().as.number;                                           \
        stack_push(value_type(a op b));                                             \
    } while (0)

    for (;;) {
#ifdef DEBUG_TRACE_STACK
        print_stack();
#endif
#ifdef DEBUG_TRACE_INSTR
        disassemble_instr(vm.chunk, vm.ip - vm.chunk->code);
#endif

        uint8_t instruction = READ_U8();
        switch (instruction) {
            case OP_NIL:      stack_push(VALUE_NIL()); break;
            case OP_TRUE:     stack_push(VALUE_BOOL(true)); break;
            case OP_FALSE:    stack_push(VALUE_BOOL(false)); break;
            case OP_CONSTANT: stack_push(READ_CONST()); break;
            case OP_POP:      stack_pop(); break;
            case OP_EQUAL:    stack_push(VALUE_BOOL(value_equals(stack_pop(), stack_pop()))); break;
            case OP_GREATER:  BINARY_OP(VALUE_BOOL, >); break;
            case OP_LESS:     BINARY_OP(VALUE_BOOL, <); break;
            case OP_ADD:
                if (is_object_type(stack_peek(0), OBJ_STRING) && is_object_type(stack_peek(1), OBJ_STRING)) {
                    const ObjString* b = (ObjString*) stack_pop().as.object;
                    const ObjString* a = (ObjString*) stack_pop().as.object;
                    stack_push(VALUE_OBJECT(concat_strings(a, b)));
                } else if (stack_peek(0).type == VAL_NUMBER && stack_peek(1).type == VAL_NUMBER) {
                    stack_push(VALUE_NUMBER(stack_pop().as.number + stack_pop().as.number));
                } else {
                    RUNTIME_ERROR("Operands must both be numbers or strings");
                    return RESULT_RUNTIME_ERROR;
                }
                break;
            case OP_SUBTRACT: BINARY_OP(VALUE_NUMBER, -); break;
            case OP_MULTIPLY: BINARY_OP(VALUE_NUMBER, *); break;
            case OP_DIVIDE:   BINARY_OP(VALUE_NUMBER, /); break;
            case OP_NOT:      stack_push(VALUE_BOOL(!value_is_truthy(stack_pop()))); break;
            case OP_NEGATE:
                if (stack_peek(0).type != VAL_NUMBER) {
                    RUNTIME_ERROR("Operand must be a number");
                    return RESULT_RUNTIME_ERROR;
                }
                (vm.stack_top - 1)->as.number *= -1;
                break;
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                hashmap_set(&vm.globals, name, stack_peek(0));
                // Pop is performed after adding to hashmap to prevent GC from
                // freeing the value after pop but before set.
                stack_pop();
            } break;
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!hashmap_get(&vm.globals, name, &value)) {
                    RUNTIME_ERROR("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
                stack_push(value);
            } break;
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                // Set doesn't pop since assignment expression should evaluate to the RHS.
                if (hashmap_set(&vm.globals, name, stack_peek(0))) {
                    hashmap_delete(&vm.globals, name);
                    RUNTIME_ERROR("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
            } break;
            case OP_GET_LOCAL: stack_push(vm.stack[READ_U8()]); break;
            case OP_SET_LOCAL: vm.stack[READ_U8()] = stack_peek(0); break;
            case OP_PRINT:
                print_value(stack_pop());
                printf("\n");
                break;
            case OP_JUMP: {
                uint16_t offset = READ_U16();
                vm.ip += offset;
            } break;
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_U16();
                if (!value_is_truthy(stack_peek(0))) vm.ip += offset;
            } break;
            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_U16();
                if (value_is_truthy(stack_peek(0))) vm.ip += offset;
            } break;
            case OP_LOOP: {
                uint16_t offset = READ_U16();
                vm.ip -= offset;
            } break;
            case OP_RETURN: return RESULT_OK;
            default:        UNREACHABLE();
        }
    }

#undef READ_U8
#undef READ_U16
#undef READ_CONST
#undef READ_STRING
#undef BINARY_OP
}

void init_vm(void) { vm.stack_top = vm.stack; }

void free_vm(void) {
    Object* current = vm.objects;
    while (current != NULL) {
        Object* next = current->next;
        free_object(current);
        current = next;
    }
    free_hashmap(&vm.strings);
    free_hashmap(&vm.globals);
}

InterpretResult interpret(const char* source) {
    Chunk chunk = {0};
    if (!compile(source, &chunk)) {
        free_chunk(&chunk);
        return RESULT_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = chunk.code;
    InterpretResult result = run();

    free_chunk(&chunk);
    return result;
}
