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

#ifdef DEBUG_TRACE_EXECUTION
static void print_stack(void) {
    printf("Stack: ");
    for (const Value* value = vm.stack; value < vm.stack_top; value++) {
        if (value != vm.stack) printf(", ");
        printf("%s", value_to_temp_cstr(*value));
    }
    printf("\n");
}
#endif

static void print_stacktrace(void) {
#ifndef HIDE_STACKTRACE
    fprintf(stderr, "Stacktrace:\n");
    for (CallFrame* frame = vm.frame; frame >= vm.frames; frame--) {
        ObjFunction* function = frame->closure->function;
        Loc loc = function->chunk.locs[frame->ip - function->chunk.code - 1];
        fprintf(stderr, "    '%s' at %u:%u\n", function->name->cstr, loc.line, loc.column);
    }
#endif
}

void runtime_error(const char* fmt, ...) {
    Chunk* chunk = &vm.frame->closure->function->chunk;
    va_list args;
    va_start(args, fmt);
    error_varg(chunk->locs[vm.frame->ip - chunk->code - 1], fmt, args);
    va_end(args);
    print_stacktrace();
}

void stack_push(Value value) {
    assert(vm.stack_top - vm.stack < STACK_SIZE && "Stack overflow");
    *(vm.stack_top++) = value;
}

Value stack_pop(void) {
    assert(vm.stack_top > vm.stack && "Stack underflow");
    return *(--vm.stack_top);
}

void stack_popn(uint8_t n) {
    vm.stack_top -= n;
    assert(vm.stack_top >= vm.stack && "Stack underflow");
}

Value stack_peek(uint32_t distance) {
    assert(distance < vm.stack_top - vm.stack && "Peek distance points outside of stack");
    return *(vm.stack_top - distance - 1);
}

static bool call(ObjClosure* closure, uint8_t arg_num) {
    if (arg_num != closure->function->arity) {
        runtime_error("Function '%s' expected %d arguments but got %d", closure->function->name->cstr,
                      closure->function->arity, arg_num);
        return false;
    }

    if (vm.frame - vm.frames + 1 == CALLSTACK_SIZE) {
        runtime_error("Stack overflow");
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
        runtime_error("Function '%s' expected %d arguments but got %d", native->name, native->arity, arg_num);
        return false;
    }

    Value return_value;
    if (!native->function(&return_value, vm.stack_top - arg_num)) return false;

    vm.stack_top -= arg_num + 1;
    stack_push(return_value);
    return true;
}

static bool call_value(Value value, uint8_t arg_num) {
    if (value.type == VAL_OBJECT) {
        switch (value.as.object->type) {
            case OBJ_CLOSURE: return call((ObjClosure*) value.as.object, arg_num);
            case OBJ_NATIVE:  return call_native((ObjNative*) value.as.object, arg_num);
            case OBJ_CLASS:   {
                ObjClass* class = (ObjClass*) value.as.object;
                Value instance = VALUE_OBJECT(new_instance(class));
                *(vm.stack_top - arg_num - 1) = instance;

                Value init_value;
                if (hashmap_get(&class->methods, vm.init_string, &init_value)) {
                    return call((ObjClosure*) init_value.as.object, arg_num);
                } else if (arg_num != 0) {
                    runtime_error("Class '%s' has no initializer, expected 0 arguments but got %d", class->name->cstr,
                                  arg_num);
                    return false;
                }

                return true;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound_method = (ObjBoundMethod*) value.as.object;
                *(vm.stack_top - arg_num - 1) = bound_method->instance;
                return call(bound_method->method, arg_num);
            }
            default: break;
        }
    }

    runtime_error("Only functions and classes can be called but found '%s'", value_to_temp_cstr(value));
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

#define UNARY_OP(op)                                                                                     \
    do {                                                                                                 \
        if (stack_peek(0).type != VAL_NUMBER) {                                                          \
            runtime_error("Operand must be a number but found '%s'", value_to_temp_cstr(stack_peek(0))); \
            return RESULT_RUNTIME_ERROR;                                                                 \
        }                                                                                                \
        (vm.stack_top - 1)->as.number op;                                                                \
        break;                                                                                           \
    } while (0)
#define BINARY_OP(value_type, op)                                                                        \
    do {                                                                                                 \
        if (stack_peek(0).type != VAL_NUMBER) {                                                          \
            runtime_error("Operands must be numbers but found '%s'", value_to_temp_cstr(stack_peek(0))); \
            return RESULT_RUNTIME_ERROR;                                                                 \
        }                                                                                                \
        if (stack_peek(1).type != VAL_NUMBER) {                                                          \
            runtime_error("Operands must be numbers but found '%s'", value_to_temp_cstr(stack_peek(1))); \
            return RESULT_RUNTIME_ERROR;                                                                 \
        }                                                                                                \
        double b = stack_pop().as.number;                                                                \
        double a = stack_pop().as.number;                                                                \
        stack_push(value_type(a op b));                                                                  \
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
            case OP_DUP:      stack_push(stack_peek(0)); break;
            case OP_POP:      stack_pop(); break;
            case OP_POPN:     stack_popn(READ_U8()); break;
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
                    Value value = (is_object_type(stack_peek(0), OBJ_STRING) || stack_peek(0).type == VAL_NUMBER)
                                      ? stack_peek(1)
                                      : stack_peek(0);
                    runtime_error("Operands must both be numbers or strings but found '%s'", value_to_temp_cstr(value));
                    return RESULT_RUNTIME_ERROR;
                }
                break;
            case OP_SUBTRACT:      BINARY_OP(VALUE_NUMBER, -); break;
            case OP_MULTIPLY:      BINARY_OP(VALUE_NUMBER, *); break;
            case OP_DIVIDE:        BINARY_OP(VALUE_NUMBER, /); break;
            case OP_NOT:           stack_push(VALUE_BOOL(!value_is_truthy(stack_pop()))); break;
            case OP_NEGATE:        UNARY_OP(*= -1); break;
            case OP_INCR:          UNARY_OP(++); break;
            case OP_DECR:          UNARY_OP(--); break;
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                hashmap_set(&vm.globals, name, stack_peek(0));
                stack_pop();
            } break;
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!hashmap_get(&vm.globals, name, &value)) {
                    runtime_error("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
                stack_push(value);
            } break;
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                // Set doesn't pop since assignment expression should evaluate to the RHS.
                if (hashmap_set(&vm.globals, name, stack_peek(0))) {
                    hashmap_delete(&vm.globals, name);
                    runtime_error("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
            } break;
            case OP_GET_LOCAL:   stack_push(vm.frame->slots[READ_U8()]); break;
            case OP_SET_LOCAL:   vm.frame->slots[READ_U8()] = stack_peek(0); break;
            case OP_GET_UPVALUE: stack_push(*vm.frame->closure->upvalues[READ_U8()]->location); break;
            case OP_SET_UPVALUE: *vm.frame->closure->upvalues[READ_U8()]->location = stack_peek(0); break;
            case OP_PRINT:       printf("%s\n", value_to_temp_cstr(stack_pop())); break;
            case OP_CONCAT:      {
                uint8_t parts = READ_U8();
                uint32_t length = 0;
                for (uint8_t i = 0; i < parts; i++) length += strlen(value_to_temp_cstr(stack_peek(i)));

                ObjString* string = create_new_string(length);
                char* current = string->cstr;
                for (int i = parts - 1; i >= 0; i--) {
                    const char* part = value_to_temp_cstr(stack_peek(i));
                    uint32_t length = strlen(part);

                    memcpy(current, part, length);
                    current += length;
                }
                stack_popn(parts);
                stack_push(VALUE_OBJECT(finish_new_string(string)));
            } break;
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
            case OP_CLASS:  stack_push(VALUE_OBJECT(new_class(READ_STRING()))); break;
            case OP_METHOD: {
                ObjClass* class = (ObjClass*) stack_peek(1).as.object;
                hashmap_set(&class->methods, READ_STRING(), stack_peek(0));
                stack_pop();
            } break;
            case OP_INHERIT: {
                Value superclass_value = stack_peek(1);
                if (!is_object_type(superclass_value, OBJ_CLASS)) {
                    runtime_error("Superclass must be a class but found '%s'", value_to_temp_cstr(superclass_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjClass* superclass = (ObjClass*) superclass_value.as.object;
                ObjClass* subclass = (ObjClass*) stack_peek(0).as.object;
                hashmap_set_all(&superclass->methods, &subclass->methods);
                stack_pop();
            } break;
            case OP_GET_FIELD: {
                Value instance_value = stack_peek(0);
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance* instance = (ObjInstance*) instance_value.as.object;
                ObjString* field = READ_STRING();

                Value value;
                if (hashmap_get(&instance->fields, field, &value)) {
                    stack_pop();
                    stack_push(value);
                    break;
                }
                if (hashmap_get(&instance->class->methods, field, &value)) {
                    ObjBoundMethod* bound_method = new_bound_method(instance_value, (ObjClosure*) value.as.object);
                    stack_pop();
                    stack_push(VALUE_OBJECT(bound_method));
                    break;
                }

                runtime_error("Undefined field '%s'", field->cstr);
                return RESULT_RUNTIME_ERROR;
            } break;
            case OP_SET_FIELD: {
                Value instance_value = stack_peek(1);
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }

                Value value = stack_peek(0);
                ObjInstance* instance = (ObjInstance*) instance_value.as.object;
                hashmap_set(&instance->fields, READ_STRING(), value);
                stack_popn(2);
                stack_push(value);
            } break;
            case OP_INVOKE: {
                ObjString* name = READ_STRING();
                uint8_t arg_num = READ_U8();
#ifdef INLINE_CACHING
                uint8_t* cache_ip = vm.frame->ip;
                vm.frame->ip += sizeof(cache_id_t) + sizeof(void*);
#endif

                Value instance_value = stack_peek(arg_num);
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance* instance = (ObjInstance*) instance_value.as.object;

                Value value;
                if (hashmap_get(&instance->fields, name, &value)) {
                    *(vm.stack_top - arg_num - 1) = value;
                    if (!call_value(value, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

#ifdef INLINE_CACHING
                if (memcmp(cache_ip, &instance->class->id, sizeof(cache_id_t)) == 0) {
                    ObjClosure* cached_method;
                    memcpy(&cached_method, cache_ip + sizeof(cache_id_t), sizeof(cached_method));
                    assert(cached_method != NULL);

                    if (!call(cached_method, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }
#endif

                if (hashmap_get(&instance->class->methods, name, &value)) {
#ifdef INLINE_CACHING
                    memcpy(cache_ip, &instance->class->id, sizeof(cache_id_t));
                    memcpy(cache_ip + sizeof(cache_id_t), &value.as.object, sizeof(void*));
#endif
                    if (!call((ObjClosure*) value.as.object, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

                runtime_error("Undefined field '%s'", name->cstr);
                return RESULT_RUNTIME_ERROR;
            } break;
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = (ObjClass*) stack_pop().as.object;

                Value value;
                if (!hashmap_get(&superclass->methods, name, &value)) {
                    runtime_error("Undefined superclass method '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }

                ObjBoundMethod* bound_method = new_bound_method(stack_peek(0), (ObjClosure*) value.as.object);
                stack_pop();
                stack_push(VALUE_OBJECT(bound_method));
            } break;
            case OP_SUPER_INVOKE: {
                ObjString* name = READ_STRING();
                uint8_t arg_num = READ_U8();
                ObjClass* superclass = (ObjClass*) stack_pop().as.object;

#ifdef INLINE_CACHING
                uint8_t* cache_ip = vm.frame->ip;
                vm.frame->ip += sizeof(void*);

                ObjClosure* cached_method;
                memcpy(&cached_method, cache_ip, sizeof(cached_method));
                if (cached_method != NULL) {
                    if (!call(cached_method, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }
#endif

                Value value;
                if (hashmap_get(&superclass->methods, name, &value)) {
#ifdef INLINE_CACHING
                    memcpy(cache_ip, &value.as.object, sizeof(void*));
#endif
                    if (!call((ObjClosure*) value.as.object, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

                runtime_error("Undefined superclass method '%s'", name->cstr);
                return RESULT_RUNTIME_ERROR;
            } break;
            default: UNREACHABLE();
        }
    }

#undef READ_U8
#undef READ_U16
#undef READ_CONST
#undef READ_STRING
#undef UNARY_OP
#undef BINARY_OP
}

void init_vm(void) {
    vm.stack_top = vm.stack;
    vm.next_gc = GC_INITIAL_THRESHOLD;

    vm.init_string = copy_string("init", 4);

    for (size_t i = 0; i < native_defs_length(); i++) {
        ObjString* name = copy_string(native_defs[i].name, strlen(native_defs[i].name));
        stack_push(VALUE_OBJECT(name));
        Value native = VALUE_OBJECT(new_native(native_defs[i]));
        stack_push(native);
        hashmap_set(&vm.globals, name, native);
        stack_popn(2);
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
