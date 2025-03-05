#ifndef CLOX_NATIVE_H_
#define CLOX_NATIVE_H_

#include "value.h"

typedef bool (*NativeFun)(Value *result, Value *args);

typedef struct {
    const char *name;
    uint8_t arity;
    NativeFun function;
} NativeDefinition;

extern NativeDefinition native_defs[];
size_t native_defs_length(void);

#endif  // CLOX_NATIVE_H_
