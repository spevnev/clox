#include "native.h"
#include <time.h>
#include "common.h"
#include "object.h"
#include "value.h"

static Value clock_fun(UNUSED(Value* args)) { return VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC); }

static Value has_field(Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) return VALUE_BOOL(false);
    if (!is_object_type(args[1], OBJ_STRING)) return VALUE_BOOL(false);

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    Value unused;
    bool has_field = hashmap_get(&instance->fields, field, &unused);
    return VALUE_BOOL(has_field);
}

static Value get_field(Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) return VALUE_BOOL(false);
    if (!is_object_type(args[1], OBJ_STRING)) return VALUE_BOOL(false);

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    Value value;
    hashmap_get(&instance->fields, field, &value);
    return value;
}

static Value set_field(Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) return VALUE_BOOL(false);
    if (!is_object_type(args[1], OBJ_STRING)) return VALUE_BOOL(false);

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    hashmap_set(&instance->fields, field, args[2]);
    return args[2];
}

static Value delete_field(Value* args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) return VALUE_BOOL(false);
    if (!is_object_type(args[1], OBJ_STRING)) return VALUE_BOOL(false);

    ObjInstance* instance = (ObjInstance*) args[0].as.object;
    ObjString* field = (ObjString*) args[1].as.object;

    hashmap_delete(&instance->fields, field);
    return VALUE_NIL();
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

size_t native_defs_length() { return sizeof(native_defs) / sizeof(NativeDefinition); }
