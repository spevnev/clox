#ifndef CLOX_VM_H_
#define CLOX_VM_H_

#include <sys/epoll.h>
#include "compiler.h"
#include "hashmap.h"
#include "object.h"
#include "value.h"

typedef enum {
    RESULT_NONE,
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

typedef struct Coroutine {
    struct Coroutine *prev;
    struct Coroutine *next;
    ObjPromise *promise;
    uint64_t sleep_time_ms;
    CallFrame *frame;
    Value *stack_top;
    CallFrame frames[CALLSTACK_SIZE];
    Value stack[STACK_SIZE];
} Coroutine;

struct EpollData;
typedef bool (*EpollCallbackFn)(struct EpollData *data);

typedef struct EpollData {
    int fd;
    bool close_fd;
    Coroutine *creator;
    EpollCallbackFn callback;
    char data[];
} EpollData;

typedef struct {
    Coroutine *active_head;
    Coroutine *sleeping_head;
    Coroutine *coroutine;
    int epoll_fd;
    uint32_t epoll_count;
    // Set of interned strings (values are always null).
    HashMap strings;
    HashMap globals;
    ObjUpvalue *open_upvalues;
    // Interned strings for comparison.
    ObjString *init_string;
    ObjString *length_string;
    // Disabled while initializing VM.
    bool enable_gc;
    Object *objects;
    uint32_t pinned_capacity;
    uint32_t pinned_length;
    Object **pinned_objects;
    uint32_t grey_capacity;
    uint32_t grey_length;
    Object **grey_objects;
    size_t allocated;
    size_t next_gc;
} VM;

extern VM vm;

uint64_t get_time_ms(void);
void runtime_error(const char *fmt, ...);
void stack_push(Value value);
Value stack_pop(void);
Value stack_peek(uint32_t distance);
void ll_add_head(Coroutine **head, Coroutine *coroutine);
Coroutine *ll_remove(Coroutine **head, Coroutine **current);
void promise_add_coroutine(ObjPromise *promise, Coroutine *coroutine);
void fulfill_promise(ObjPromise *promise, Value value);
void *vm_epoll_add(int fd, uint32_t epoll_events, EpollCallbackFn callback, size_t callback_data_size);
void vm_epoll_delete(EpollData *epoll_data);
InterpretResult schedule_coroutine(void);
void init_vm(void);
void free_vm(void);
InterpretResult interpret(const char *source);

#endif  // CLOX_VM_H_
