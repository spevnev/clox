#include "native.h"
#include <time.h>
#include "common.h"
#include "error.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static bool clock_fun(Value* result, UNUSED(Value* args)) {
    *result = VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC);
    return true;
}

static bool has_field(Value* result, Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument of hasField must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument of hasField must be a string");
        return false;
    }

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    Value unused;
    bool has_field = hashmap_get(&instance->fields, field, &unused);
    *result = VALUE_BOOL(has_field);
    return true;
}

static bool get_field(Value* result, Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument of getField must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument of getField must be a string");
        return false;
    }

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    if (!hashmap_get(&instance->fields, field, result)) {
        runtime_error("Undefined field '%s'", field->cstr);
        return false;
    }
    return true;
}

static bool set_field(Value* result, Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument of setField must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument of setField must be a string");
        return false;
    }

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    hashmap_set(&instance->fields, field, args[2]);
    *result = args[2];
    return true;
}

static bool delete_field(Value* result, Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument of deleteField must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument of deleteField must be a string");
        return false;
    }

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    hashmap_delete(&instance->fields, field);
    *result = VALUE_NIL();
    return true;
}

NativeDefinition native_defs[] = {
    // clang-format off
    // name,         arity, function
    { "clock",       0,     clock_fun    },
    { "hasField",    2,     has_field    },
    { "getField",    2,     get_field    },
    { "setField",    3,     set_field    },
    { "deleteField", 2,     delete_field },
    // clang-format on
};

size_t native_defs_length(void) { return sizeof(native_defs) / sizeof(NativeDefinition); }
