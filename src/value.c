#include "value.h"
#include <string.h>
#include "common.h"
#include "error.h"
#include "memory.h"

void values_push(ValueVec *vec, Value value) {
    if (vec->length >= vec->capacity) {
        uint32_t old_capacity = vec->capacity;
        vec->capacity = VEC_GROW_CAPACITY(vec->capacity);
        vec->values = ARRAY_REALLOC(vec->values, old_capacity, vec->capacity);
    }

    vec->values[vec->length++] = value;
}

void print_value(Value value) {
    switch (value.type) {
        case VAL_NIL:    printf("nil"); break;
        case VAL_BOOL:   printf("%s", value.as.boolean ? "true" : "false"); break;
        case VAL_NUMBER: printf("%g", value.as.number); break;
        case VAL_OBJECT: print_object(value.as.object); break;
        default:         UNREACHABLE();
    }
}

bool value_is_truthy(Value value) {
    switch (value.type) {
        case VAL_NIL:  return false;
        case VAL_BOOL: return value.as.boolean;
        default:       return true;
    }
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL:    return true;
        case VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.number == b.as.number;
        case VAL_OBJECT: return a.as.object == b.as.object;
        default:         UNREACHABLE();
    }
}
