#include "vm.h"
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "native.h"
#include "value.h"

VM vm = {0};

#define RUNTIME_ERROR(...)                                                   \
    do {                                                                     \
        Chunk* chunk = &vm.frame->closure->function->chunk;                  \
        ERROR_AT(chunk->lines[vm.frame->ip - chunk->code - 1], __VA_ARGS__); \
        print_stacktrace();                                                  \
    } while (0)

static bool is_object_type(Value value, ObjectType type) {
    return value.type == VAL_OBJECT && value.as.object->type == type;
}

__attribute__((unused)) static void print_stack(void) {
    printf("Stack: ");
    for (const Value* value = vm.stack; value < vm.stack_top; value++) {
        if (value != vm.stack) printf(", ");
        print_value(*value);
    }
    printf("\n");
}

static void print_stacktrace(void) {
    fprintf(stderr, "Stacktrace:\n");
    for (CallFrame* frame = vm.frame; frame >= vm.frames; frame--) {
        ObjFunction* function = frame->closure->function;
        uint32_t line = function->chunk.lines[frame->ip - function->chunk.code - 1];
        fprintf(stderr, "    '%s' at line %u\n", function->name->cstr, line);
    }
}

void stack_push(Value value) {
    assert(vm.stack_top - vm.stack < STACK_SIZE && "Stack overflow");
    *(vm.stack_top++) = value;
}

Value stack_pop(void) {
    assert(vm.stack_top > vm.stack && "Stack underflow");
    return *(--vm.stack_top);
}

Value stack_peek(uint32_t distance) {
    assert(distance < vm.stack_top - vm.stack && "Peek distance points outside of stack");
    return *(vm.stack_top - 1 - distance);
}

static bool call(ObjClosure* closure, uint8_t arg_num) {
    if (arg_num != closure->function->arity) {
        RUNTIME_ERROR("Function '%s' expected %d arguments but got %d", closure->function->name->cstr,
                      closure->function->arity, arg_num);
        return false;
    }

    if (vm.frame - vm.frames + 1 == CALLSTACK_SIZE) {
        RUNTIME_ERROR("Stack overflow");
        return false;
    }

    CallFrame* frame = ++vm.frame;
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack_top - arg_num - 1;

    return true;
}

static bool call_native(ObjNative* native, uint8_t arg_num) {
    if (arg_num != native->arity) {
        RUNTIME_ERROR("Function '%s' expected %d arguments but got %d", native->name, native->arity, arg_num);
        return false;
    }

    Value return_value = native->function(vm.stack_top - arg_num, arg_num);
    vm.stack_top -= arg_num + 1;
    stack_push(return_value);

    return true;
}

static bool call_value(Value value, uint8_t arg_num) {
    if (value.type == VAL_OBJECT) {
        switch (value.as.object->type) {
            case OBJ_CLOSURE: return call((ObjClosure*) value.as.object, arg_num);
            case OBJ_NATIVE:  return call_native((ObjNative*) value.as.object, arg_num);
            case OBJ_CLASS:
                vm.stack_top -= arg_num + 1;  // Ignore arguments.
                stack_push(VALUE_OBJECT(new_instance((ObjClass*) value.as.object)));
                return true;
            default: break;
        }
    }

    RUNTIME_ERROR("Called value must be functions or classes");
    return false;
}

static ObjUpvalue* capture_upvalue(Value* value) {
    ObjUpvalue *prev = NULL, *current = vm.open_upvalues;
    while (current != NULL && current->location > value) {
        prev = current;
        current = current->next;
    }

    if (current != NULL && current->location == value) return current;

    ObjUpvalue* new = new_upvalue(value);
    new->next = current;
    if (prev == NULL) {
        vm.open_upvalues = new;
    } else {
        prev->next = new;
    }

    return new;
}

static void close_upvalues(Value* value) {
    ObjUpvalue* current = vm.open_upvalues;
    while (current != NULL && current->location >= value) {
        current->closed = *current->location;
        current->location = &current->closed;
        current = current->next;
    }
    vm.open_upvalues = current;
}

static InterpretResult run(void) {
#define READ_U8() (*vm.frame->ip++)
#define READ_U16() (vm.frame->ip += 2, (uint16_t) (*(vm.frame->ip - 2) | (*(vm.frame->ip - 1) << 8)))
#define READ_CONST() (vm.frame->closure->function->chunk.constants.values[READ_U8()])
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
#ifdef DEBUG_TRACE_EXECUTION
        print_stack();
        disassemble_instr(&vm.frame->closure->function->chunk, vm.frame->ip - vm.frame->closure->function->chunk.code);
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
            case OP_GET_LOCAL:   stack_push(vm.frame->slots[READ_U8()]); break;
            case OP_SET_LOCAL:   vm.frame->slots[READ_U8()] = stack_peek(0); break;
            case OP_GET_UPVALUE: stack_push(*vm.frame->closure->upvalues[READ_U8()]->location); break;
            case OP_SET_UPVALUE: *vm.frame->closure->upvalues[READ_U8()]->location = stack_peek(0); break;
            case OP_PRINT:
                print_value(stack_pop());
                printf("\n");
                break;
            case OP_JUMP: {
                uint16_t offset = READ_U16();
                vm.frame->ip += offset;
            } break;
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_U16();
                if (!value_is_truthy(stack_peek(0))) vm.frame->ip += offset;
            } break;
            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_U16();
                if (value_is_truthy(stack_peek(0))) vm.frame->ip += offset;
            } break;
            case OP_LOOP: {
                uint16_t offset = READ_U16();
                vm.frame->ip -= offset;
            } break;
            case OP_CALL: {
                uint8_t arg_num = READ_U8();
                if (!call_value(stack_peek(arg_num), arg_num)) return RESULT_RUNTIME_ERROR;
            } break;
            case OP_CLOSURE: {
                ObjClosure* closure = new_closure((ObjFunction*) READ_CONST().as.object);
                stack_push(VALUE_OBJECT(closure));

                for (uint32_t i = 0; i < closure->upvalues_length; i++) {
                    uint8_t is_local = READ_U8();
                    uint8_t index = READ_U8();

                    if (is_local) {
                        closure->upvalues[i] = capture_upvalue(&vm.frame->slots[index]);
                    } else {
                        closure->upvalues[i] = vm.frame->closure->upvalues[index];
                    }
                }
            } break;
            case OP_CLOSE_UPVALUE:
                close_upvalues(vm.stack_top - 1);
                stack_pop();
                break;
            case OP_RETURN: {
                // Save return value.
                Value return_value = stack_pop();

                if (vm.frame == vm.frames) {
                    // Initial callframe, pop script's closure and exit.
                    stack_pop();
                    return RESULT_OK;
                }

                // Close upvalues and pop local variables.
                close_upvalues(vm.frame->slots);
                vm.stack_top = vm.frame->slots;

                // Pop frame.
                vm.frame--;

                // Restore return value.
                stack_push(return_value);
            } break;
            case OP_CLASS:     stack_push(VALUE_OBJECT(new_class(READ_STRING()))); break;
            case OP_GET_FIELD: {
                if (!is_object_type(stack_peek(0), OBJ_INSTANCE)) {
                    RUNTIME_ERROR("Only instances have fields");
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance* instance = (ObjInstance*) stack_pop().as.object;
                ObjString* field = READ_STRING();

                Value value;
                if (!hashmap_get(&instance->fields, field, &value)) {
                    RUNTIME_ERROR("Undefined field '%s'", field->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
                stack_push(value);
            } break;
            case OP_SET_FIELD: {
                if (!is_object_type(stack_peek(1), OBJ_INSTANCE)) {
                    RUNTIME_ERROR("Only instances have fields");
                    return RESULT_RUNTIME_ERROR;
                }

                Value value = stack_pop();
                ObjInstance* instance = (ObjInstance*) stack_pop().as.object;
                stack_push(value);

                hashmap_set(&instance->fields, READ_STRING(), value);
            } break;
            default: UNREACHABLE();
        }
    }

#undef READ_U8
#undef READ_U16
#undef READ_CONST
#undef READ_STRING
#undef BINARY_OP
}

void init_vm(void) {
    vm.stack_top = vm.stack;
    vm.next_gc = GC_INITIAL_THRESHOLD;

    for (size_t i = 0; i < native_defs_length(); i++) {
        ObjString* name = copy_string(native_defs[i].name, strlen(native_defs[i].name));
        stack_push(VALUE_OBJECT(name));
        Value native = VALUE_OBJECT(new_native(native_defs[i]));
        stack_push(native);
        hashmap_set(&vm.globals, name, native);
        stack_pop();
        stack_pop();
    }
}

void free_vm(void) {
    free_hashmap(&vm.strings);
    free_hashmap(&vm.globals);

    Object* current = vm.objects;
    while (current != NULL) {
        Object* next = current->next;
        free_object(current);
        current = next;
    }
    free(vm.grey_objects);
}

InterpretResult interpret(const char* source) {
    ObjFunction* script = compile(source);
    if (script == NULL) return RESULT_COMPILE_ERROR;

    stack_push(VALUE_OBJECT(script));
    ObjClosure* closure = new_closure(script);
    stack_pop();
    stack_push(VALUE_OBJECT(closure));

    // Create initial callframe.
    CallFrame* frame = vm.frame = vm.frames;
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack_top - 1;

    return run();
}
