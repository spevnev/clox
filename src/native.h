#ifndef CLOX_NATIVE_H_
#define CLOX_NATIVE_H_

#include "value.h"

typedef Value (*NativeFun)(Value *args, uint8_t arg_num);

typedef struct {
    const char *name;
    uint8_t arity;
    NativeFun function;
} NativeDefinition;

extern NativeDefinition native_defs[];
int native_defs_length();

#endif  // CLOX_NATIVE_H_
