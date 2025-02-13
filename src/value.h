#ifndef CLOX_VALUE_H_
#define CLOX_VALUE_H_

#include "common.h"

typedef double Value;

typedef struct {
    uint32_t capacity;
    uint32_t length;
    Value *values;
} ValueVec;

void values_push(ValueVec *vec, Value value);
void print_value(Value value);

#endif  // CLOX_VALUE_H_
