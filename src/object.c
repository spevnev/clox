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
        case OBJ_STRING: return ((const ObjString *) object)->cstr;
        case OBJ_FUNCTION:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjFunction *) object)->name->cstr);
            return CSTR;
        case OBJ_UPVALUE: return "upvalue";
        case OBJ_CLOSURE:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjClosure *) object)->function->name->cstr);
            return CSTR;
        case OBJ_NATIVE: snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjNative *) object)->name); return CSTR;
        case OBJ_CLASS:  return ((const ObjClass *) object)->name->cstr;
        case OBJ_INSTANCE:
            snprintf(CSTR, sizeof(CSTR), "%s instance", ((const ObjInstance *) object)->class->name->cstr);
            return CSTR;
        case OBJ_BOUND_METHOD:
            snprintf(CSTR, sizeof(CSTR), "<fn %s>", ((const ObjBoundMethod *) object)->method->function->name->cstr);
            return CSTR;
        case OBJ_PROMISE: return "<Promise>";
        default:          UNREACHABLE();
    }
}

void free_object(Object *object) {
    switch (object->type) {
        case OBJ_STRING: FREE(object, sizeof(ObjString) + ((ObjString *) object)->length + 1); break;
        case OBJ_FUNCTION:
            free_chunk(&((ObjFunction *) object)->chunk);
            FREE(object, sizeof(ObjFunction));
            break;
        case OBJ_UPVALUE: FREE(object, sizeof(ObjUpvalue)); break;
        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *) object;
            FREE(object, sizeof(ObjClosure) + sizeof(*closure->upvalues) * closure->upvalues_length);
        } break;
        case OBJ_NATIVE: FREE(object, sizeof(ObjNative)); break;
        case OBJ_CLASS:  {
            ObjClass *class = (ObjClass *) object;
            free_hashmap(&class->methods);
            FREE(object, sizeof(ObjClass));
        } break;
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *) object;
            free_hashmap(&instance->fields);
            FREE(object, sizeof(ObjInstance));
        } break;
        case OBJ_BOUND_METHOD: FREE(object, sizeof(ObjBoundMethod)); break;
        case OBJ_PROMISE:      FREE(object, sizeof(ObjPromise)); break;
        default:               UNREACHABLE();
    }
}

static Object *new_object(ObjectType type, uint32_t size) {
    Object *object = ALLOC(size);
    object->is_marked = false;
    object->is_root = false;
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

ObjNative *new_native(NativeFunctionDef definition) {
    ObjNative *native = (ObjNative *) new_object(OBJ_NATIVE, sizeof(ObjNative));
    native->name = definition.name;
    native->arity = definition.arity;
    native->function = definition.function;
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

ObjPromise *new_promise(void) {
    ObjPromise *promise = (ObjPromise *) new_object(OBJ_PROMISE, sizeof(ObjPromise));
    promise->is_fulfilled = false;
    promise->next = NULL;
    promise->data.coroutines.head = NULL;
    promise->data.coroutines.tail = NULL;
    return promise;
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

ObjString *create_new_string(uint32_t capacity) {
    assert(capacity > 0);
    ObjString *string = (ObjString *) new_object(OBJ_STRING, sizeof(ObjString) + capacity + 1);
    // Make it null-terminated to avoid segfault if its printed.
    string->cstr[0] = '\0';
    string->length = 0;
    return string;
}

ObjString *finish_new_string(ObjString *string, uint32_t length) {
    string->cstr[length] = '\0';
    string->length = length;
    string->hash = hash_string(string->cstr, length);

    ObjString *interned_string = hashmap_find_key(&vm.strings, string->cstr, length, string->hash);
    if (interned_string != NULL) return interned_string;

    stack_push(VALUE_OBJECT(string));
    hashmap_set(&vm.strings, string, VALUE_NIL());
    stack_pop();
    return string;
}

void object_disable_gc(Object *object) {
    assert(object->is_root == false);
    if (vm.root_length >= vm.root_capacity) {
        vm.root_capacity = OBJECTS_GROW_CAPACITY(vm.root_capacity);
        vm.root_objects = realloc(vm.root_objects, sizeof(*vm.root_objects) * vm.root_capacity);
        if (vm.root_objects == NULL) OUT_OF_MEMORY();
    }
    vm.root_objects[vm.root_length++] = object;
    object->is_root = true;
}

void object_enable_gc(Object *object) {
    assert(object->is_root == true);
    object->is_root = false;
}
