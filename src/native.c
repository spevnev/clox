#include "native.h"
#include <string.h>
#include <time.h>
#include "common.h"
#include "error.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static bool clock_(Value *result, UNUSED(Value *args)) {
    *result = VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC);
    return true;
}

static bool has_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    Value unused;
    bool has_field = hashmap_get(&instance->fields, field, &unused);
    *result = VALUE_BOOL(has_field);
    return true;
}

static bool get_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    if (!hashmap_get(&instance->fields, field, result)) {
        runtime_error("Undefined field '%s'", field->cstr);
        return false;
    }
    return true;
}

static bool set_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    hashmap_set(&instance->fields, field, args[2]);
    *result = args[2];
    return true;
}

static bool delete_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    hashmap_delete(&instance->fields, field);
    *result = VALUE_NIL();
    return true;
}

static NativeFunctionDef functions[] = {
    // clang-format off
    // name,         arity, function
    { "clock",        0,    clock_           },

    { "hasField",     2,    has_field        },
    { "getField",     2,    get_field        },
    { "setField",     3,    set_field        },
    { "deleteField",  2,    delete_field     },
    // clang-format on
};

void create_native_functions(void) {
    for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++) {
        ObjString *name = copy_string(functions[i].name, strlen(functions[i].name));
        Value function = VALUE_OBJECT(new_native(functions[i]));
        hashmap_set(&vm.globals, name, function);
    }
}
