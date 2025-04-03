#ifndef CLOX_VM_H_
#define CLOX_VM_H_

#include "chunk.h"
#include "compiler.h"
#include "hashmap.h"
#include "object.h"
#include "value.h"

typedef enum {
    RESULT_OK,
    RESULT_COMPILE_ERROR,
    RESULT_RUNTIME_ERROR,
} InterpretResult;

#define CALLSTACK_SIZE 64
#define STACK_SIZE (CALLSTACK_SIZE * LOCALS_SIZE)

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

// TODO: Reduce size
typedef struct Coroutine {
    struct Coroutine *prev;
    struct Coroutine *next;
    ObjPromise *promise;
    CallFrame *frame;
    Value *stack_top;
    CallFrame frames[CALLSTACK_SIZE];
    Value stack[STACK_SIZE];
} Coroutine;

typedef struct {
    Coroutine *coroutines_head;
    Coroutine *coroutine;
    // Set of interned strings (values are always null).
    HashMap strings;
    HashMap globals;
    ObjUpvalue *open_upvalues;
    ObjString *init_string;
    // Disabled while initializing VM.
    bool enable_gc;
    Object *objects;
    uint32_t grey_capacity;
    uint32_t grey_length;
    Object **grey_objects;
    size_t allocated;
    size_t next_gc;
} VM;

extern VM vm;

void init_vm(void);
void free_vm(void);
void runtime_error(const char *fmt, ...);
void stack_push(Value value);
Value stack_pop(void);
Value stack_peek(uint32_t distance);
InterpretResult interpret(const char *source);

#endif  // CLOX_VM_H_
