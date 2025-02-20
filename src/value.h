#ifndef CLOX_VALUE_H_
#define CLOX_VALUE_H_

#include "common.h"

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
    } as;
} Value;

#define VALUE_NIL() ((Value) {.type = VAL_NIL})
#define VALUE_BOOL(value) ((Value) {.type = VAL_BOOL, .as.boolean = (value)})
#define VALUE_NUMBER(value) ((Value) {.type = VAL_NUMBER, .as.number = (value)})

typedef struct {
    uint32_t capacity;
    uint32_t length;
    Value *values;
} ValueVec;

void values_push(ValueVec *vec, Value value);
void print_value(Value value);
bool value_is_truthy(Value value);
bool value_equals(Value a, Value b);

#endif  // CLOX_VALUE_H_
