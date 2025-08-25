#define _POSIX_C_SOURCE 200809L
#include "vm.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "native.h"
#include "value.h"

VM vm = {0};

uint64_t get_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) PANIC("Error in clock_gettime: %s", strerror(errno));
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

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
    va_list args;
    va_start(args, fmt);
    Chunk *chunk = &vm.coroutine->frame->closure->function->chunk;
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
    if (coroutine == NULL) OUT_OF_MEMORY();
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

void promise_add_coroutine(ObjPromise *promise, Coroutine *coroutine) {
    assert(!promise->is_fulfilled);

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

void fulfill_promise(ObjPromise *promise, Value value) {
    assert(!promise->is_fulfilled);

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

// Checks timers on all sleeping coroutines, wakes up if the timer has finished.
// Returns the minimum time in miliseconds until the soonest one wakes up.
static uint64_t check_sleeping_coroutines(void) {
    uint64_t current_ms = get_time_ms();
    uint64_t min_wait_ms = UINT64_MAX;
    Coroutine *current = vm.sleeping_head;
    while (current != NULL) {
        if (current->sleep_time_ms <= current_ms) {
            Coroutine *removed = ll_remove(&vm.sleeping_head, &current);
            ll_add_head(&vm.active_head, removed);
        } else {
            if (current->sleep_time_ms < min_wait_ms) min_wait_ms = current->sleep_time_ms;
            current = current->next;
        }
    }
    if (min_wait_ms == UINT64_MAX) return UINT64_MAX;
    return min_wait_ms - current_ms;
}

void *vm_epoll_add(int fd, uint32_t epoll_events, EpollCallbackFn callback, size_t callback_data_size) {
    EpollData *epoll_data = malloc(sizeof(*epoll_data) + callback_data_size);
    if (epoll_data == NULL) OUT_OF_MEMORY();
    epoll_data->fd = fd;
    epoll_data->close_fd = false;
    epoll_data->creator = vm.coroutine;
    epoll_data->callback = callback;

    struct epoll_event event = {.events = epoll_events, .data.ptr = epoll_data};

    bool retried = false;
retry:
    if (epoll_ctl(vm.epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
        if (retried || errno != EEXIST) PANIC("Error in epoll_ctl: %s", strerror(errno));

        // Duplicate file descriptor, mark it for closure on epoll_delete, and retry.
        fd = dup(fd);
        epoll_data->fd = fd;
        epoll_data->close_fd = true;
        retried = true;
        goto retry;
    }

    vm.epoll_count++;
    return epoll_data->data;
}

void vm_epoll_delete(EpollData *epoll_data) {
    if (epoll_ctl(vm.epoll_fd, EPOLL_CTL_DEL, epoll_data->fd, NULL) != 0) {
        PANIC("Error in epoll_ctl: %s", strerror(errno));
    }
    vm.epoll_count--;
    if (epoll_data->close_fd) close(epoll_data->fd);
    free(epoll_data);
}

// Checks epoll for events and executes callbacks that may wake up coroutines.
static bool check_polling_coroutines(uint64_t ms) {
    const int max_events = 16;
    struct epoll_event events[max_events];

    int events_num = epoll_wait(vm.epoll_fd, events, max_events, ms);
    if (events_num == -1) PANIC("Error in epoll_wait: %s", strerror(errno));

    for (int i = 0; i < events_num; i++) {
        EpollData *epoll_data = events[i].data.ptr;

        // Set current coroutine to the one that created the event for the callback.
        Coroutine *current = vm.coroutine;
        vm.coroutine = epoll_data->creator;
        if (!epoll_data->callback(epoll_data)) return false;
        vm.coroutine = current;
    }

    return true;
}

InterpretResult schedule_coroutine(void) {
    assert(vm.coroutine == NULL);

    for (;;) {
        // Check sleeping coroutines and keep timer until the soonest coroutine.
        uint64_t min_wait_ms = check_sleeping_coroutines();

        // Check IO events without blocking.
        if (!check_polling_coroutines(0)) return RESULT_RUNTIME_ERROR;

        // There are currently active coroutines.
        if (vm.active_head != NULL) {
            // Start again from the head.
            vm.coroutine = vm.active_head;
            return RESULT_NONE;
        }

        // There are no sleeping and no polling coroutines, finish execution.
        if (min_wait_ms == UINT64_MAX && vm.epoll_count == 0) return RESULT_OK;

        // Block until sleeping coroutine wakes up, or IO event happens.
        if (!check_polling_coroutines(min_wait_ms)) return RESULT_RUNTIME_ERROR;
    }
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
#define ARRAY_UNARY_OP(op)                                                                                       \
    do {                                                                                                         \
        if (!check_int_arg(stack_peek(0), 0, UINT32_MAX)) {                                                      \
            runtime_error("Index must be a positive integer but found '%s'", value_to_temp_cstr(stack_peek(0))); \
            return RESULT_RUNTIME_ERROR;                                                                         \
        }                                                                                                        \
        uint32_t index = (uint32_t) stack_pop().as.number;                                                       \
        Value array_value = stack_pop();                                                                         \
        if (!is_object_type(array_value, OBJ_ARRAY)) {                                                           \
            runtime_error("Expected an array but found '%s'", value_to_temp_cstr(array_value));                  \
            return RESULT_RUNTIME_ERROR;                                                                         \
        }                                                                                                        \
        ObjArray *array = (ObjArray *) array_value.as.object;                                                    \
        if (index >= array->length) {                                                                            \
            runtime_error("Index out of bounds");                                                                \
            return RESULT_RUNTIME_ERROR;                                                                         \
        }                                                                                                        \
        Value element = array->elements[index];                                                                  \
        if (element.type != VAL_NUMBER) {                                                                        \
            runtime_error("Operand must be a number but found '%s'", value_to_temp_cstr(element));               \
            return RESULT_RUNTIME_ERROR;                                                                         \
        }                                                                                                        \
        array->elements[index].as.number op;                                                                     \
        stack_push(element);                                                                                     \
    } while (0)

#define SCHEDULE_COROUTINE()                           \
    if (vm.coroutine == NULL) {                        \
        InterpretResult result = schedule_coroutine(); \
        if (result != RESULT_NONE) return result;      \
    }

    for (;;) {
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
                stack_push(VALUE_OBJECT(finish_new_string(string, length)));
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
                    SCHEDULE_COROUTINE();
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
                ObjString *field = READ_STRING();

                if (is_object_type(instance_value, OBJ_INSTANCE)) {
                    ObjInstance *instance = (ObjInstance *) instance_value.as.object;

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
                } else if (is_object_type(instance_value, OBJ_STRING)) {
                    if (field != vm.length_string) {
                        runtime_error("Undefined field '%s', strings only have length", field->cstr);
                        return RESULT_RUNTIME_ERROR;
                    }

                    ObjString *string = (ObjString *) instance_value.as.object;
                    stack_pop();
                    stack_push(VALUE_NUMBER(string->length));
                } else if (is_object_type(instance_value, OBJ_ARRAY)) {
                    if (field != vm.length_string) {
                        runtime_error("Undefined field '%s', arrays only have length", field->cstr);
                        return RESULT_RUNTIME_ERROR;
                    }

                    ObjArray *array = (ObjArray *) instance_value.as.object;
                    stack_pop();
                    stack_push(VALUE_NUMBER(array->length));
                } else {
                    runtime_error("Fields only exist on instances but found '%s'", value_to_temp_cstr(instance_value));
                    return RESULT_RUNTIME_ERROR;
                }
            } break;
            case OP_SET_FIELD: {
                Value instance_value = stack_peek(1);
                ObjString *field = READ_STRING();
                if (!is_object_type(instance_value, OBJ_INSTANCE)) {
                    if ((is_object_type(instance_value, OBJ_ARRAY) || is_object_type(instance_value, OBJ_STRING))
                        && field == vm.length_string) {
                        runtime_error("Cannot assign to length, it is immutable");
                    } else {
                        runtime_error("Fields only exist on instances but found '%s'",
                                      value_to_temp_cstr(instance_value));
                    }
                    return RESULT_RUNTIME_ERROR;
                }
                ObjInstance *instance = (ObjInstance *) instance_value.as.object;

                Value value = stack_peek(0);
                hashmap_set(&instance->fields, field, value);
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
                    memcpy(&cached_method, cache_ip + sizeof(cache_id_t), sizeof(void *));
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
                memcpy(&cached_method, cache_ip, sizeof(void *));
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
            case OP_YIELD:
                vm.coroutine = vm.coroutine->next;
                SCHEDULE_COROUTINE();
                break;
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
                    SCHEDULE_COROUTINE();
                }
            } break;
            case OP_ARRAY: {
                uint32_t elements = READ_U8();
                ObjArray *array = new_array(elements, VALUE_NIL());
                stack_popn(elements);
                memcpy(array->elements, vm.coroutine->stack_top, elements * sizeof(*array->elements));
                stack_push(VALUE_OBJECT(array));
            } break;
            case OP_ARRAY_GET: {
                if (!check_int_arg(stack_peek(0), 0, UINT32_MAX)) {
                    runtime_error("Index must be a positive integer but found '%s'", value_to_temp_cstr(stack_peek(0)));
                    return RESULT_RUNTIME_ERROR;
                }
                uint32_t index = (uint32_t) stack_pop().as.number;

                Value value = stack_pop();
                if (is_object_type(value, OBJ_ARRAY)) {
                    ObjArray *array = (ObjArray *) value.as.object;
                    if (index >= array->length) {
                        runtime_error("Index out of bounds");
                        return RESULT_RUNTIME_ERROR;
                    }

                    stack_push(array->elements[index]);
                } else if (is_object_type(value, OBJ_STRING)) {
                    ObjString *string = (ObjString *) value.as.object;
                    if (index >= string->length) {
                        runtime_error("Index out of bounds");
                        return RESULT_RUNTIME_ERROR;
                    }

                    stack_push(VALUE_OBJECT(copy_string(&string->cstr[index], 1)));
                } else {
                    runtime_error("Expected an array or a string but found '%s'", value_to_temp_cstr(value));
                    return RESULT_RUNTIME_ERROR;
                }
            } break;
            case OP_ARRAY_SET: {
                Value value = stack_pop();

                if (!check_int_arg(stack_peek(0), 0, UINT32_MAX)) {
                    runtime_error("Index must be a positive integer but found '%s'", value_to_temp_cstr(stack_peek(0)));
                    return RESULT_RUNTIME_ERROR;
                }
                uint32_t index = (uint32_t) stack_pop().as.number;

                Value array_value = stack_pop();
                if (!is_object_type(array_value, OBJ_ARRAY)) {
                    runtime_error("Expected an array but found '%s'", value_to_temp_cstr(array_value));
                    return RESULT_RUNTIME_ERROR;
                }
                ObjArray *array = (ObjArray *) array_value.as.object;

                if (index >= array->length) {
                    runtime_error("Index out of bounds");
                    return RESULT_RUNTIME_ERROR;
                }
                array->elements[index] = value;
                stack_push(value);
            } break;
            case OP_ARRAY_INCR: ARRAY_UNARY_OP(++); break;
            case OP_ARRAY_DECR: ARRAY_UNARY_OP(--); break;
            default:            UNREACHABLE();
        }
    }

#undef READ_U8
#undef READ_U16
#undef READ_CONST
#undef READ_STRING
#undef UNARY_OP
#undef BINARY_OP
#undef ARRAY_UNARY_OP
#undef SCHEDULE_COROUTINE
}

void init_vm(void) {
    vm.coroutine = vm.active_head = new_coroutine();
    vm.epoll_fd = epoll_create1(0);
    if (vm.epoll_fd == -1) PANIC("Error in epoll_create: %s", strerror(errno));
    vm.next_gc = GC_INITIAL_THRESHOLD;
    vm.init_string = copy_string("init", 4);
    vm.length_string = copy_string("length", 6);

    add_native_functions();
}

void free_vm(void) {
    close(vm.epoll_fd);
    free(vm.pinned_objects);
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
