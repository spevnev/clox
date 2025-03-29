#include "object.h"
#include <assert.h>
#include <string.h>
#include "common.h"
#include "error.h"
#include "hashmap.h"
#include "memory.h"
#include "vm.h"

#ifdef INLINE_CACHING
cache_id_t next_id(void) {
    // Ids start at 1 to reserve 0 for uninitialized cached.
    static cache_id_t id = 1;

    assert(id != CACHE_ID_MAX);
    return id++;
}
#endif

const char *object_to_temp_cstr(const Object *object) {
    static char CSTR[1024];

    switch (object->type) {
        case OBJ_UPVALUE: return "upvalue";
        case OBJ_STRING:  return ((const ObjString *) object)->cstr;
        case OBJ_CLASS:   return ((const ObjClass *) object)->name->cstr;
        case OBJ_FUNCTION:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjFunction *) object)->name->cstr);
            return CSTR;
        case OBJ_CLOSURE:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjClosure *) object)->function->name->cstr);
            return CSTR;
        case OBJ_NATIVE: snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjNative *) object)->name); return CSTR;
        case OBJ_INSTANCE:
            snprintf(CSTR, sizeof(CSTR), "%s instance", ((const ObjInstance *) object)->class->name->cstr);
            return CSTR;
        case OBJ_BOUND_METHOD:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjBoundMethod *) object)->method->function->name->cstr);
            return CSTR;
        default: UNREACHABLE();
    }
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
            reallocate(object, sizeof(ObjClosure) + sizeof(*closure->upvalues) * closure->upvalues_length, 0);
        } break;
        case OBJ_NATIVE: reallocate(object, sizeof(ObjNative), 0); break;
        case OBJ_CLASS:  {
            ObjClass *class = (ObjClass *) object;
            free_hashmap(&class->methods);
            reallocate(object, sizeof(ObjClass), 0);
        } break;
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *) object;
            free_hashmap(&instance->fields);
            reallocate(object, sizeof(ObjInstance), 0);
        } break;
        case OBJ_BOUND_METHOD: reallocate(object, sizeof(ObjBoundMethod), 0); break;
        default:               UNREACHABLE();
    }
}

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

ObjFunction *new_function(ObjString *name, bool is_async) {
    ObjFunction *function = (ObjFunction *) new_object(OBJ_FUNCTION, sizeof(ObjFunction));
    function->name = name;
    function->is_async = is_async;
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
    size_t upvalues_size = sizeof(ObjUpvalue *) * function->upvalues_count;
    ObjClosure *closure = (ObjClosure *) new_object(OBJ_CLOSURE, sizeof(ObjClosure) + upvalues_size);
    closure->function = function;
    closure->upvalues_length = function->upvalues_count;
    memset(closure->upvalues, 0, upvalues_size);
    return closure;
}

ObjNative *new_native(NativeDefinition def) {
    ObjNative *native = (ObjNative *) new_object(OBJ_NATIVE, sizeof(ObjNative));
    native->name = def.name;
    native->arity = def.arity;
    native->function = def.function;
    return native;
}

ObjClass *new_class(ObjString *name) {
    ObjClass *class = (ObjClass *) new_object(OBJ_CLASS, sizeof(ObjClass));
    class->name = name;
    class->methods = (HashMap) {0};
#ifdef INLINE_CACHING
    class->id = next_id();
#endif
    return class;
}

ObjInstance *new_instance(ObjClass *class) {
    ObjInstance *instance = (ObjInstance *) new_object(OBJ_INSTANCE, sizeof(ObjInstance));
    instance->class = class;
    instance->fields = (HashMap) {0};
    return instance;
}

ObjBoundMethod *new_bound_method(Value instance, ObjClosure *method) {
    ObjBoundMethod *bound_method = (ObjBoundMethod *) new_object(OBJ_BOUND_METHOD, sizeof(ObjBoundMethod));
    bound_method->instance = instance;
    bound_method->method = method;
    return bound_method;
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
    uint32_t new_length = a->length + b->length;
    uint32_t size = sizeof(ObjString) + new_length + 1;

    stack_push(VALUE_OBJECT(a));
    stack_push(VALUE_OBJECT(b));
    ObjString *string = (ObjString *) new_object(OBJ_STRING, size);
    stack_pop();
    stack_pop();

    memcpy(string->cstr, a->cstr, a->length);
    memcpy(string->cstr + a->length, b->cstr, b->length);
    string->cstr[new_length] = '\0';
    string->length = new_length;
    string->hash = hash_string(string->cstr, string->length);

    ObjString *interned_string = hashmap_find_key(&vm.strings, string->cstr, string->length, string->hash);
    if (interned_string != NULL) return interned_string;

    stack_push(VALUE_OBJECT(string));
    hashmap_set(&vm.strings, string, VALUE_NIL());
    stack_pop();

    return string;
}

ObjString *create_new_string(uint32_t length) {
    ObjString *string = (ObjString *) new_object(OBJ_STRING, sizeof(ObjString) + length + 1);
    string->cstr[length] = '\0';
    string->length = length;
    return string;
}

ObjString *finish_new_string(ObjString *string) {
    string->hash = hash_string(string->cstr, string->length);

    ObjString *interned_string = hashmap_find_key(&vm.strings, string->cstr, string->length, string->hash);
    if (interned_string != NULL) return interned_string;

    stack_push(VALUE_OBJECT(string));
    hashmap_set(&vm.strings, string, VALUE_NIL());
    stack_pop();
    return string;
}
