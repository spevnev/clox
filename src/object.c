#include "object.h"
#include <string.h>
#include "common.h"
#include "error.h"
#include "hashmap.h"
#include "memory.h"
#include "vm.h"

static Object *new_object(ObjectType type, uint32_t size) {
    Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

ObjFunction *new_function(ObjString *name) {
    ObjFunction *function = (ObjFunction *) new_object(OBJ_FUNCTION, sizeof(ObjFunction));
    function->name = name;
    function->arity = 0;
    function->upvalues_count = 0;
    function->chunk = (Chunk) {0};
    return function;
}

ObjUpvalue *new_upvalue(Value *value) {
    ObjUpvalue *upvalue = (ObjUpvalue *) new_object(OBJ_UPVALUE, sizeof(ObjUpvalue));
    upvalue->location = value;
    upvalue->next = NULL;
    return upvalue;
}

ObjClosure *new_closure(ObjFunction *function) {
    ObjClosure *closure = (ObjClosure *) new_object(OBJ_CLOSURE, sizeof(ObjClosure));
    closure->function = function;
    closure->upvalues = reallocate(NULL, 0, sizeof(*closure->upvalues) * function->upvalues_count);
    for (uint32_t i = 0; i < function->upvalues_count; i++) closure->upvalues[i] = NULL;
    closure->upvalues_length = function->upvalues_count;
    return closure;
}

ObjNative *new_native(NativeDefinition def) {
    ObjNative *native = (ObjNative *) new_object(OBJ_NATIVE, sizeof(ObjNative));
    native->name = def.name;
    native->arity = def.arity;
    native->function = def.function;
    return native;
}

void free_object(Object *object) {
    switch (object->type) {
        case OBJ_STRING: reallocate(object, sizeof(ObjString) + ((ObjString *) object)->length + 1, 0); break;
        case OBJ_FUNCTION:
            free_chunk(&((ObjFunction *) object)->chunk);
            reallocate(object, sizeof(ObjFunction), 0);
            break;
        case OBJ_UPVALUE: reallocate(object, sizeof(ObjUpvalue), 0); break;
        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *) object;
            ARRAY_REALLOC(closure->upvalues, closure->upvalues_length, 0);
            reallocate(object, sizeof(ObjClosure), 0);
        } break;
        case OBJ_NATIVE: reallocate(object, sizeof(ObjNative), 0); break;
        default:         UNREACHABLE();
    }
}

void print_object(const Object *object) {
    switch (object->type) {
        case OBJ_STRING:   printf("%s", ((const ObjString *) object)->cstr); break;
        case OBJ_FUNCTION: printf("<fn %s>", ((const ObjFunction *) object)->name->cstr); break;
        case OBJ_CLOSURE:  printf("<fn %s>", ((const ObjClosure *) object)->function->name->cstr); break;
        case OBJ_NATIVE:   printf("<native fn %s>", ((const ObjNative *) object)->name); break;
        default:           UNREACHABLE();
    }
}

ObjString *copy_string(const char *cstr, uint32_t length) {
    uint32_t hash = hash_string(cstr, length);
    ObjString *interned_string = hashmap_find_key(&vm.strings, cstr, length, hash);
    if (interned_string != NULL) return interned_string;

    ObjString *string = (ObjString *) new_object(OBJ_STRING, sizeof(ObjString) + length + 1);
    string->hash = hash;
    memcpy(string->cstr, cstr, length);
    string->cstr[length] = '\0';
    string->length = length;

    hashmap_set(&vm.strings, string, VALUE_NIL());
    return string;
}

ObjString *concat_strings(const ObjString *a, const ObjString *b) {
    uint32_t new_length = a->length + b->length;
    uint32_t size = sizeof(ObjString) + new_length + 1;

    ObjString *string = (ObjString *) new_object(OBJ_STRING, size);
    memcpy(string->cstr, a->cstr, a->length);
    memcpy(string->cstr + a->length, b->cstr, b->length);
    string->cstr[new_length] = '\0';
    string->length = new_length;
    string->hash = hash_string(string->cstr, string->length);

    ObjString *interned_string = hashmap_find_key(&vm.strings, string->cstr, string->length, string->hash);
    if (interned_string != NULL) {
        reallocate(string, size, 0);
        return interned_string;
    }

    return string;
}
