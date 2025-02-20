#include "value.h"
#include "common.h"
#include "error.h"
#include "memory.h"

void values_push(ValueVec *vec, Value value) {
    if (vec->length >= vec->capacity) {
        uint32_t old_capacity = vec->capacity;
        vec->capacity = VEC_GROW_CAPACITY(vec->capacity);
        vec->values = VEC_REALLOC(vec->values, old_capacity, vec->capacity);
    }

    vec->values[vec->length++] = value;
}

void print_value(Value value) {
    switch (value.type) {
        case VAL_NIL:    printf("nil"); break;
        case VAL_BOOL:   printf("%s", value.as.boolean ? "true" : "false"); break;
        case VAL_NUMBER: printf("%g", value.as.number); break;
        default:         UNREACHABLE();
    }
}
