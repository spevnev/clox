#ifndef CLOX_VALUE_H_
#define CLOX_VALUE_H_

#include "common.h"

typedef struct Object Object;

typedef enum {
    VAL_NIL = 0,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJECT,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Object *object;
    } as;
} Value;

typedef struct {
    uint32_t capacity;
    uint32_t length;
    Value *values;
} ValueVec;

#define VALUE_NIL() ((Value) {.type = VAL_NIL})
#define VALUE_BOOL(value) ((Value) {.type = VAL_BOOL, .as.boolean = (value)})
#define VALUE_NUMBER(value) ((Value) {.type = VAL_NUMBER, .as.number = (value)})
#define VALUE_OBJECT(value) ((Value) {.type = VAL_OBJECT, .as.object = (Object *) (value)})

void values_push(ValueVec *vec, Value value);
void print_value(Value value);
bool value_is_truthy(Value value);
bool value_equals(Value a, Value b);

#endif  // CLOX_VALUE_H_
