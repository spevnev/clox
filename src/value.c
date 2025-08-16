#include "value.h"
#include <math.h>
#include "error.h"
#include "memory.h"
#include "object.h"

bool check_int_arg(Value arg, double min, double max) {
    if (arg.type != VAL_NUMBER) return false;

    double temp;
    double number = arg.as.number;
    return (min <= number && number <= max) && modf(number, &temp) == 0.0;
}

const char *value_to_temp_cstr(Value value) {
    static char CSTR[1024];

    switch (value.type) {
        case VAL_NIL:    return "nil";
        case VAL_BOOL:   return value.as.boolean ? "true" : "false";
        case VAL_NUMBER: {
            int length = snprintf(CSTR, sizeof(CSTR), "%.10f", value.as.number);
            // Remove trailing zeroes
            while (CSTR[length - 1] == '0') length--;
            if (CSTR[length - 1] == '.') length--;
            CSTR[length] = '\0';
            return CSTR;
        }
        case VAL_OBJECT: return object_to_temp_cstr(value.as.object);
        default:         UNREACHABLE();
    }
}

void values_push(ValueVec *vec, Value value) {
    if (vec->length >= vec->capacity) {
        uint32_t old_capacity = vec->capacity;
        vec->capacity = VEC_GROW_CAPACITY(vec->capacity);
        vec->values = ARRAY_REALLOC(vec->values, old_capacity, vec->capacity);
    }

    vec->values[vec->length++] = value;
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
