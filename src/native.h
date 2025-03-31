#ifndef CLOX_NATIVE_H_
#define CLOX_NATIVE_H_

#include "value.h"

typedef bool (*NativeFn)(Value *result, Value *args);

typedef struct {
    const char *name;
    uint8_t arity;
    NativeFn function;
} NativeFunctionDef;

// Creates native functions and adds to VM's globals.
void create_native_functions(void);

#endif  // CLOX_NATIVE_H_
