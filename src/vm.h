#ifndef CLOX_VM_H_
#define CLOX_VM_H_

#include "chunk.h"
#include "value.h"

typedef enum {
    RESULT_OK,
    RESULT_COMPILE_ERROR,
    RESULT_RUNTIME_ERROR,
} InterpretResult;

#define STACK_SIZE 256

typedef struct {
    Chunk* chunk;
    uint8_t* ip;
    Value stack[STACK_SIZE];
    Value* stack_top;
    Object* objects;
} VM;

extern VM vm;

void init_vm();
void free_vm();
InterpretResult interpret(const char* source);

#endif  // CLOX_VM_H_
