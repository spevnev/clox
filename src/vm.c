#define _POSIX_C_SOURCE 200809L
#include "vm.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
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
    for (const Value *value = vm.coroutine->stack; value < vm.coroutine->stack_top; value++) {
        if (value != vm.coroutine->stack) printf(", ");
        printf("%s", value_to_temp_cstr(*value));
    }
    printf("\n");
}
#endif

#ifdef HIDE_STACKTRACE
static void print_stacktrace(void) {}
#else
static void print_stacktrace(void) {
    fprintf(stderr, "Stacktrace:\n");
    for (CallFrame *frame = vm.coroutine->frame; frame >= vm.coroutine->frames; frame--) {
        ObjFunction *function = frame->closure->function;
        Loc loc = function->chunk.locs[frame->ip - function->chunk.code - 1];
        fprintf(stderr, "    '%s' at %u:%u\n", function->name->cstr, loc.line, loc.column);
    }
}
#endif

void runtime_error(const char *fmt, ...) {
    Chunk *chunk = &vm.coroutine->frame->closure->function->chunk;
    va_list args;
    va_start(args, fmt);
    error_varg(chunk->locs[vm.coroutine->frame->ip - chunk->code - 1], fmt, args);
    va_end(args);
    print_stacktrace();
}

static void coroutine_stack_push(Coroutine *coroutine, Value value) {
    assert(coroutine->stack_top - coroutine->stack < STACK_SIZE && "Stack overflow");
    *(coroutine->stack_top++) = value;
}

void stack_push(Value value) { coroutine_stack_push(vm.coroutine, value); }

Value stack_pop(void) {
    assert(vm.coroutine->stack_top > vm.coroutine->stack && "Stack underflow");
    return *(--vm.coroutine->stack_top);
}

void stack_popn(uint8_t n) {
    vm.coroutine->stack_top -= n;
    assert(vm.coroutine->stack_top >= vm.coroutine->stack && "Stack underflow");
}

Value stack_peek(uint32_t distance) {
    assert(distance < vm.coroutine->stack_top - vm.coroutine->stack && "Peek distance points outside of stack");
    return *(vm.coroutine->stack_top - distance - 1);
}

static Coroutine *new_coroutine(void) {
    Coroutine *coroutine = malloc(sizeof(*coroutine));
    coroutine->prev = NULL;
    coroutine->next = NULL;
    coroutine->promise = new_promise();
    coroutine->frame = NULL;
    coroutine->stack_top = coroutine->stack;
    return coroutine;
}

// Creates the first callframe in the new coroutine.
static void init_callstack(Coroutine *coroutine, ObjClosure *closure) {
    CallFrame *frame = coroutine->frame = coroutine->frames;
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = coroutine->stack;
}

void ll_add_head(Coroutine **head, Coroutine *coroutine) {
    coroutine->prev = NULL;
    coroutine->next = *head;
    if (*head != NULL) (*head)->prev = coroutine;
    *head = coroutine;
}

Coroutine *ll_remove(Coroutine **head, Coroutine **current) {
    if ((*current)->next != NULL) (*current)->next->prev = (*current)->prev;

    if ((*current)->prev == NULL) {
        *head = (*current)->next;
    } else {
        (*current)->prev->next = (*current)->next;
    }

    Coroutine *removed = *current;
    *current = removed->next;
    return removed;
}

static void vm_add_coroutine_before(Coroutine *coroutine) {
    assert(vm.coroutine != NULL);

    coroutine->prev = vm.coroutine->prev;
    coroutine->next = vm.coroutine;

    if (vm.coroutine->prev == NULL) {
        vm.active_head = coroutine;
    } else {
        vm.coroutine->prev->next = coroutine;
    }
    vm.coroutine->prev = coroutine;
}

static bool call(ObjClosure *closure, uint8_t arg_num) {
    if (arg_num != closure->function->arity) {
        runtime_error("Function '%s' expected %d arguments but got %d", closure->function->name->cstr,
                      closure->function->arity, arg_num);
        return false;
    }

    if (closure->function->is_async) {
        Coroutine *coroutine = new_coroutine();
        init_callstack(coroutine, closure);
        vm_add_coroutine_before(coroutine);

        // Move arguments from callee's stack to the new coroutine.
        vm.coroutine->stack_top -= arg_num + 1;
        coroutine->stack_top = coroutine->stack + arg_num + 1;
        memcpy(coroutine->stack, vm.coroutine->stack_top, sizeof(*coroutine->stack) * (arg_num + 1));

        // Push promise to the callee's stack.
        stack_push(VALUE_OBJECT(coroutine->promise));

        vm.coroutine = coroutine;
    } else {
        if (vm.coroutine->frame - vm.coroutine->frames + 1 == CALLSTACK_SIZE) {
            runtime_error("Stack overflow");
            return false;
        }

        CallFrame *frame = ++vm.coroutine->frame;
        frame->closure = closure;
        frame->ip = closure->function->chunk.code;
        frame->slots = vm.coroutine->stack_top - arg_num - 1;
    }

    return true;
}

static bool call_native(ObjNative *native, uint8_t arg_num) {
    if (arg_num != native->arity) {
        runtime_error("Function '%s' expected %d arguments but got %d", native->name, native->arity, arg_num);
        return false;
    }

    Coroutine *callee = vm.coroutine;

    Value return_value;
    if (!native->function(&return_value, callee->stack_top - arg_num)) return false;

    // Don't use `vm.coroutine` directly after native function since it may have changed it.
    callee->stack_top -= arg_num + 1;
    coroutine_stack_push(callee, return_value);

    return true;
}

static bool call_value(Value value, uint8_t arg_num) {
    if (value.type == VAL_OBJECT) {
        switch (value.as.object->type) {
            case OBJ_CLOSURE: return call((ObjClosure *) value.as.object, arg_num);
            case OBJ_NATIVE:  return call_native((ObjNative *) value.as.object, arg_num);
            case OBJ_CLASS:   {
                ObjClass *class = (ObjClass *) value.as.object;
                Value instance = VALUE_OBJECT(new_instance(class));
                *(vm.coroutine->stack_top - arg_num - 1) = instance;

                Value init_value;
                if (hashmap_get(&class->methods, vm.init_string, &init_value)) {
                    return call((ObjClosure *) init_value.as.object, arg_num);
                } else if (arg_num != 0) {
                    runtime_error("Class '%s' has no initializer, expected 0 arguments but got %d", class->name->cstr,
                                  arg_num);
                    return false;
                }

                return true;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound_method = (ObjBoundMethod *) value.as.object;
                *(vm.coroutine->stack_top - arg_num - 1) = bound_method->instance;
                return call(bound_method->method, arg_num);
            }
            default: break;
        }
    }

    runtime_error("Only functions and classes can be called but found '%s'", value_to_temp_cstr(value));
    return false;
}

static ObjUpvalue *capture_upvalue(Value *value) {
    ObjUpvalue *prev = NULL, *current = vm.open_upvalues;
    while (current != NULL && current->location > value) {
        prev = current;
        current = current->next;
    }

    if (current != NULL && current->location == value) return current;

    ObjUpvalue *new = new_upvalue(value);
    new->next = current;
    if (prev == NULL) {
        vm.open_upvalues = new;
    } else {
        prev->next = new;
    }

    return new;
}

static void close_upvalues(Value *value) {
    ObjUpvalue *current = vm.open_upvalues;
    while (current != NULL && current->location >= value) {
        current->closed = *current->location;
        current->location = &current->closed;
        current = current->next;
    }
    vm.open_upvalues = current;
}

static void promise_add_coroutine(ObjPromise *promise, Coroutine *coroutine) {
    assert(promise->is_fulfilled == false);

    if (promise->data.coroutines.head == NULL) {
        coroutine->prev = NULL;
        coroutine->next = NULL;
        promise->data.coroutines.head = coroutine;
        promise->data.coroutines.tail = coroutine;
    } else {
        coroutine->prev = promise->data.coroutines.tail;
        coroutine->next = NULL;
        promise->data.coroutines.tail->next = coroutine;
        promise->data.coroutines.tail = coroutine;
    }
}

static void fulfill_promise(ObjPromise *promise, Value value) {
    assert(promise->is_fulfilled == false);

    // Merge waiting coroutines with active ones.
    if (promise->data.coroutines.head != NULL) {
        for (Coroutine *current = promise->data.coroutines.head; current != NULL; current = current->next) {
            // Overwrite the promise at the top of the stack with the value.
            *(current->stack_top - 1) = value;
        }

        promise->data.coroutines.tail->next = vm.active_head;
        if (vm.active_head != NULL) vm.active_head->prev = promise->data.coroutines.tail;
        vm.active_head = promise->data.coroutines.head;
    }

    promise->is_fulfilled = true;
    promise->data.value = value;

    for (ObjPromise *current = promise->next; current != NULL; current = current->next) {
        fulfill_promise(current, value);
    }
}

#define NS_IN_SEC 1000000000L

static uint64_t get_time_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) PANIC("Unexpected error in clock_gettime: %s", strerror(errno));
    return ts.tv_sec * NS_IN_SEC + ts.tv_nsec;
}

static void sleep_ns(uint64_t ns) {
    struct timespec ts = {.tv_sec = ns / NS_IN_SEC, .tv_nsec = ns % NS_IN_SEC};
    if (nanosleep(&ts, NULL) != 0) PANIC("Unexpected error in nanosleep: %s", strerror(errno));
}

// Updates timers on all sleeping coroutines, wakes up if the timer has finished.
// Returns the minimum timer in nanoseconds.
static uint64_t update_sleeping_coroutines(uint64_t time_diff_ns) {
    uint64_t min_wait_ns = UINT64_MAX;
    Coroutine *current = vm.sleeping_head;
    while (current != NULL) {
        if (current->sleep_time_ns <= time_diff_ns) {
            Coroutine *removed = ll_remove(&vm.sleeping_head, &current);
            ll_add_head(&vm.active_head, removed);
        } else {
            current->sleep_time_ns -= time_diff_ns;
            if (current->sleep_time_ns < min_wait_ns) min_wait_ns = current->sleep_time_ns;

            current = current->next;
        }
    }
    return min_wait_ns;
}

static InterpretResult run(void) {
#define READ_U8() (*vm.coroutine->frame->ip++)
#define READ_U16() \
    (vm.coroutine->frame->ip += 2, (uint16_t) (*(vm.coroutine->frame->ip - 2) | (*(vm.coroutine->frame->ip - 1) << 8)))
#define READ_CONST() (vm.coroutine->frame->closure->function->chunk.constants.values[READ_U8()])
#define READ_STRING() ((ObjString *) READ_CONST().as.object)

#define UNARY_OP(op)                                                                                     \
    do {                                                                                                 \
        if (stack_peek(0).type != VAL_NUMBER) {                                                          \
            runtime_error("Operand must be a number but found '%s'", value_to_temp_cstr(stack_peek(0))); \
            return RESULT_RUNTIME_ERROR;                                                                 \
        }                                                                                                \
        (vm.coroutine->stack_top - 1)->as.number op;                                                     \
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

    uint64_t time_ns = get_time_ns();
    for (;;) {
        // After iteration over all active coroutines, check the sleeping ones.
        if (vm.coroutine == NULL) {
            uint64_t new_time_ns = get_time_ns();
            // TODO: Accumulate time difference and only update once min_wait_ns has passed.
            uint64_t min_wait_ns = update_sleeping_coroutines(new_time_ns - time_ns);
            time_ns = new_time_ns;

            // There are no active coroutines.
            if (vm.active_head == NULL) {
                // There are no sleeping coroutines.
                if (min_wait_ns == UINT64_MAX) return RESULT_OK;

                // Sleep until the next sleeping coroutine wakes up.
                sleep_ns(min_wait_ns);
                continue;
            }

            // Start again from the head.
            vm.coroutine = vm.active_head;
        }

#ifdef DEBUG_TRACE_EXECUTION
        print_stack();
        const Chunk *chunk = &vm.coroutine->frame->closure->function->chunk;
        disassemble_instr(chunk, vm.coroutine->frame->ip - chunk->code);
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
                    const ObjString *b = (ObjString *) stack_pop().as.object;
                    const ObjString *a = (ObjString *) stack_pop().as.object;
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
                ObjString *name = READ_STRING();
                hashmap_set(&vm.globals, name, stack_peek(0));
                stack_pop();
            } break;
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!hashmap_get(&vm.globals, name, &value)) {
                    runtime_error("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
                stack_push(value);
            } break;
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                // Set doesn't pop since assignment expression should evaluate to the RHS.
                if (hashmap_set(&vm.globals, name, stack_peek(0))) {
                    hashmap_delete(&vm.globals, name);
                    runtime_error("Undefined variable '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }
            } break;
            case OP_GET_LOCAL:   stack_push(vm.coroutine->frame->slots[READ_U8()]); break;
            case OP_SET_LOCAL:   vm.coroutine->frame->slots[READ_U8()] = stack_peek(0); break;
            case OP_GET_UPVALUE: stack_push(*vm.coroutine->frame->closure->upvalues[READ_U8()]->location); break;
            case OP_SET_UPVALUE: *vm.coroutine->frame->closure->upvalues[READ_U8()]->location = stack_peek(0); break;
            case OP_PRINT:       printf("%s\n", value_to_temp_cstr(stack_pop())); break;
            case OP_CONCAT:      {
                uint8_t parts = READ_U8();
                uint32_t length = 0;
                for (uint8_t i = 0; i < parts; i++) length += strlen(value_to_temp_cstr(stack_peek(i)));

                ObjString *string = create_new_string(length);
                char *current = string->cstr;
                for (int i = parts - 1; i >= 0; i--) {
                    const char *part = value_to_temp_cstr(stack_peek(i));
                    uint32_t length = strlen(part);

                    memcpy(current, part, length);
                    current += length;
                }
                stack_popn(parts);
                stack_push(VALUE_OBJECT(finish_new_string(string)));
            } break;
            case OP_JUMP: {
                uint16_t offset = READ_U16();
                vm.coroutine->frame->ip += offset;
            } break;
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_U16();
                if (!value_is_truthy(stack_peek(0))) vm.coroutine->frame->ip += offset;
            } break;
            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_U16();
                if (value_is_truthy(stack_peek(0))) vm.coroutine->frame->ip += offset;
            } break;
            case OP_LOOP: {
                uint16_t offset = READ_U16();
                vm.coroutine->frame->ip -= offset;
            } break;
            case OP_CALL: {
                uint8_t arg_num = READ_U8();
                if (!call_value(stack_peek(arg_num), arg_num)) return RESULT_RUNTIME_ERROR;
            } break;
            case OP_CLOSURE: {
                ObjClosure *closure = new_closure((ObjFunction *) READ_CONST().as.object);
                stack_push(VALUE_OBJECT(closure));

                for (uint32_t i = 0; i < closure->upvalues_length; i++) {
                    uint8_t is_local = READ_U8();
                    uint8_t index = READ_U8();

                    if (is_local) {
                        closure->upvalues[i] = capture_upvalue(&vm.coroutine->frame->slots[index]);
                    } else {
                        closure->upvalues[i] = vm.coroutine->frame->closure->upvalues[index];
                    }
                }
            } break;
            case OP_CLOSE_UPVALUE:
                close_upvalues(vm.coroutine->stack_top - 1);
                stack_pop();
                break;
            case OP_RETURN: {
                // Save return value.
                Value return_value = stack_pop();

                // Close upvalues.
                close_upvalues(vm.coroutine->frame->slots);

                // Check if it's the last callframe in the coroutine.
                if (vm.coroutine->frame == vm.coroutine->frames) {
                    Coroutine *finished = ll_remove(&vm.active_head, &vm.coroutine);
                    if (is_object_type(return_value, OBJ_PROMISE)) {
                        ObjPromise *promise = (ObjPromise *) return_value.as.object;
                        if (promise->is_fulfilled) {
                            fulfill_promise(finished->promise, promise->data.value);
                        } else {
                            promise->next = finished->promise;
                        }
                    } else {
                        fulfill_promise(finished->promise, return_value);
                    }
                    free(finished);
                } else {
                    // Pop frame and its stack.
                    vm.coroutine->stack_top = vm.coroutine->frame->slots;
                    vm.coroutine->frame--;

                    // Restore return value.
                    stack_push(return_value);
                }
            } break;
            case OP_CLASS:  stack_push(VALUE_OBJECT(new_class(READ_STRING()))); break;
            case OP_METHOD: {
                ObjClass *class = (ObjClass *) stack_peek(1).as.object;
                hashmap_set(&class->methods, READ_STRING(), stack_peek(0));
                stack_pop();
            } break;
            case OP_INHERIT: {
                Value superclass_value = stack_peek(1);
                if (!is_object_type(superclass_value, OBJ_CLASS)) {
                    runtime_error("Superclass must be a class but found '%s'", value_to_temp_cstr(superclass_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjClass *superclass = (ObjClass *) superclass_value.as.object;
                ObjClass *subclass = (ObjClass *) stack_peek(0).as.object;
                hashmap_set_all(&superclass->methods, &subclass->methods);
                stack_pop();
            } break;
            case OP_GET_FIELD: {
                Value instance_value = stack_peek(0);
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance *instance = (ObjInstance *) instance_value.as.object;
                ObjString *field = READ_STRING();

                Value value;
                if (hashmap_get(&instance->fields, field, &value)) {
                    stack_pop();
                    stack_push(value);
                    break;
                }
                if (hashmap_get(&instance->class->methods, field, &value)) {
                    ObjBoundMethod *bound_method = new_bound_method(instance_value, (ObjClosure *) value.as.object);
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
                ObjInstance *instance = (ObjInstance *) instance_value.as.object;

                Value value = stack_peek(0);
                hashmap_set(&instance->fields, READ_STRING(), value);
                stack_popn(2);
                stack_push(value);
            } break;
            case OP_INVOKE: {
                ObjString *name = READ_STRING();
                uint8_t arg_num = READ_U8();
#ifdef INLINE_CACHING
                uint8_t *cache_ip = vm.coroutine->frame->ip;
                vm.coroutine->frame->ip += sizeof(cache_id_t) + sizeof(void *);
#endif

                Value instance_value = stack_peek(arg_num);
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance *instance = (ObjInstance *) instance_value.as.object;

                Value value;
                if (hashmap_get(&instance->fields, name, &value)) {
                    *(vm.coroutine->stack_top - arg_num - 1) = value;
                    if (!call_value(value, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

#ifdef INLINE_CACHING
                if (memcmp(cache_ip, &instance->class->id, sizeof(cache_id_t)) == 0) {
                    ObjClosure *cached_method;
                    memcpy(&cached_method, cache_ip + sizeof(cache_id_t), sizeof(cached_method));
                    assert(cached_method != NULL);

                    if (!call(cached_method, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }
#endif

                if (hashmap_get(&instance->class->methods, name, &value)) {
#ifdef INLINE_CACHING
                    memcpy(cache_ip, &instance->class->id, sizeof(cache_id_t));
                    memcpy(cache_ip + sizeof(cache_id_t), &value.as.object, sizeof(void *));
#endif
                    if (!call((ObjClosure *) value.as.object, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

                runtime_error("Undefined field '%s'", name->cstr);
                return RESULT_RUNTIME_ERROR;
            } break;
            case OP_GET_SUPER: {
                ObjString *name = READ_STRING();
                ObjClass *superclass = (ObjClass *) stack_pop().as.object;

                Value value;
                if (!hashmap_get(&superclass->methods, name, &value)) {
                    runtime_error("Undefined superclass method '%s'", name->cstr);
                    return RESULT_RUNTIME_ERROR;
                }

                ObjBoundMethod *bound_method = new_bound_method(stack_peek(0), (ObjClosure *) value.as.object);
                stack_pop();
                stack_push(VALUE_OBJECT(bound_method));
            } break;
            case OP_SUPER_INVOKE: {
                ObjString *name = READ_STRING();
                uint8_t arg_num = READ_U8();
                ObjClass *superclass = (ObjClass *) stack_pop().as.object;

#ifdef INLINE_CACHING
                uint8_t *cache_ip = vm.coroutine->frame->ip;
                vm.coroutine->frame->ip += sizeof(void *);

                ObjClosure *cached_method;
                memcpy(&cached_method, cache_ip, sizeof(cached_method));
                if (cached_method != NULL) {
                    if (!call(cached_method, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }
#endif

                Value value;
                if (hashmap_get(&superclass->methods, name, &value)) {
#ifdef INLINE_CACHING
                    memcpy(cache_ip, &value.as.object, sizeof(void *));
#endif
                    if (!call((ObjClosure *) value.as.object, arg_num)) return RESULT_RUNTIME_ERROR;
                    break;
                }

                runtime_error("Undefined superclass method '%s'", name->cstr);
                return RESULT_RUNTIME_ERROR;
            } break;
            case OP_YIELD: vm.coroutine = vm.coroutine->next; break;
            case OP_AWAIT: {
                Value promise_value = stack_peek(0);
                if (!is_object_type(promise_value, OBJ_PROMISE)) {
                    runtime_error("Operand must be a promise but found '%s'", value_to_temp_cstr(promise_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjPromise *promise = (ObjPromise *) promise_value.as.object;

                if (promise->is_fulfilled) {
                    stack_pop();
                    stack_push(promise->data.value);
                } else {
                    Coroutine *waiting = ll_remove(&vm.active_head, &vm.coroutine);
                    promise_add_coroutine(promise, waiting);
                }
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
    vm.coroutine = vm.active_head = new_coroutine();
    vm.next_gc = GC_INITIAL_THRESHOLD;
    vm.init_string = copy_string("init", 4);

    add_native_functions();
}

void free_vm(void) {
    free(vm.grey_objects);
    free_hashmap(&vm.strings);
    free_hashmap(&vm.globals);

    for (Object *current = vm.objects; current != NULL;) {
        Object *next = current->next;
        free_object(current);
        current = next;
    }

    for (Coroutine *current = vm.active_head; current != NULL;) {
        Coroutine *next = current->next;
        free(current);
        current = next;
    }
}

InterpretResult interpret(const char *source) {
    ObjFunction *script = compile(source);
    if (script == NULL) return RESULT_COMPILE_ERROR;

    ObjClosure *closure = new_closure(script);
    init_callstack(vm.coroutine, closure);
    stack_push(VALUE_OBJECT(closure));

    vm.enable_gc = true;
    return run();
}
