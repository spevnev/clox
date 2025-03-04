#include "object.h"
#include <string.h>
#include "common.h"
#include "error.h"
#include "hashmap.h"
#include "memory.h"
#include "vm.h"

static Object *new_object(ObjectType type, uint32_t size) {
    Object *object = reallocate(NULL, 0, size);
    object->is_marked = false;
    object->type = type;
    object->next = vm.objects;
    vm.objects = object;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %u for type %d\n", (void *) object, size, type);
#endif
    return object;
}

ObjFunction *new_function(ObjString *name) {
    stack_push(VALUE_OBJECT(name));
    ObjFunction *function = (ObjFunction *) new_object(OBJ_FUNCTION, sizeof(ObjFunction));
    stack_pop();
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
    ObjUpvalue **upvalues = reallocate(NULL, 0, sizeof(*upvalues) * function->upvalues_count);
    for (uint32_t i = 0; i < function->upvalues_count; i++) upvalues[i] = NULL;

    ObjClosure *closure = (ObjClosure *) new_object(OBJ_CLOSURE, sizeof(ObjClosure));
    closure->function = function;
    closure->upvalues = upvalues;
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
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *) object, object->type);
#endif

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
        case OBJ_UPVALUE:  printf("upvalue"); break;
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

    stack_push(VALUE_OBJECT(string));
    hashmap_set(&vm.strings, string, VALUE_NIL());
    stack_pop();

    return string;
}

ObjString *concat_strings(const ObjString *a, const ObjString *b) {
    stack_push(VALUE_OBJECT(a));
    stack_push(VALUE_OBJECT(b));

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
        stack_pop();
        stack_pop();
        return interned_string;
    }

    stack_push(VALUE_OBJECT(string));
    hashmap_set(&vm.strings, string, VALUE_NIL());
    stack_pop();
    stack_pop();
    stack_pop();

    return string;
}
