#ifndef CLOX_VM_H_
#define CLOX_VM_H_

#include "chunk.h"
#include "hashmap.h"
#include "object.h"
#include "value.h"

typedef enum {
    RESULT_OK,
    RESULT_COMPILE_ERROR,
    RESULT_RUNTIME_ERROR,
} InterpretResult;

#define CALLSTACK_SIZE 64
#define STACK_SIZE (CALLSTACK_SIZE * (UINT8_MAX + 1))

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct {
    CallFrame frames[CALLSTACK_SIZE];
    uint32_t frames_length;
    Value stack[STACK_SIZE];
    Value* stack_top;
    Object* objects;
    HashMap strings;  // Set of interned strings (values are always null).
    HashMap globals;
    ObjUpvalue* open_upvalues;
} VM;

extern VM vm;

void init_vm(void);
void free_vm(void);
InterpretResult interpret(const char* source);

#endif  // CLOX_VM_H_
