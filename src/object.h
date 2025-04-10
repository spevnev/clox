#ifndef CLOX_OBJECT_H_
#define CLOX_OBJECT_H_

#include "chunk.h"
#include "common.h"
#include "hashmap.h"
#include "native.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_UPVALUE,
    OBJ_CLOSURE,
    OBJ_NATIVE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_PROMISE,
    OBJ_ARRAY,
} ObjectType;

typedef struct Object {
    bool is_marked;
    uint8_t pin_count;
    ObjectType type;
    struct Object *next;
} Object;

typedef struct ObjString {
    Object object;
    uint32_t hash;
    uint32_t length;
    char cstr[];
} ObjString;

typedef struct {
    Object object;
    ObjString *name;
    bool is_async;
    uint8_t arity;
    uint32_t upvalues_count;
    Chunk chunk;
} ObjFunction;

typedef struct ObjUpvalue {
    Object object;
    Value closed;
    Value *location;
    struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
    Object object;
    ObjFunction *function;
    uint32_t upvalues_length;
    ObjUpvalue *upvalues[];
} ObjClosure;

typedef struct {
    Object object;
    const char *name;
    uint8_t arity;
    NativeFn function;
} ObjNative;

typedef struct {
    Object object;
    ObjString *name;
    HashMap methods;
#ifdef INLINE_CACHING
    uint16_t id;
#endif
} ObjClass;

typedef struct {
    Object object;
    ObjClass *class;
    HashMap fields;
} ObjInstance;

typedef struct {
    Object object;
    Value instance;
    ObjClosure *method;
} ObjBoundMethod;

typedef struct ObjPromise {
    Object object;
    bool is_fulfilled;
    // Linked list of promises that are waiting for this one to be fulfilled.
    struct ObjPromise *next;
    union {
        Value value;
        struct {
            struct Coroutine *tail;
            struct Coroutine *head;
        } coroutines;
    } data;
} ObjPromise;

typedef struct {
    Object object;
    uint32_t length;
    Value elements[];
} ObjArray;

#ifdef INLINE_CACHING
typedef uint16_t cache_id_t;
#define CACHE_ID_MAX UINT16_MAX

// Id to compare classes in inline cache.
cache_id_t next_id(void);
#endif

const char *object_to_temp_cstr(const Object *object);
void free_object(Object *object);
ObjFunction *new_function(ObjString *name, bool is_async);
ObjUpvalue *new_upvalue(Value *value);
ObjClosure *new_closure(ObjFunction *function);
ObjNative *new_native(NativeFunctionDef definition);
ObjClass *new_class(ObjString *name);
ObjInstance *new_instance(ObjClass *class);
ObjBoundMethod *new_bound_method(Value instance, ObjClosure *method);
ObjPromise *new_promise(void);
ObjArray *new_array(uint32_t size, Value fill_value);
ObjString *copy_string(const char *cstr, uint32_t length);
ObjString *concat_strings(const ObjString *a, const ObjString *b);
// Create a new string of the given length for callee to fill `cstr`.
// After filling in, callee must `finish_new_string`.
ObjString *create_new_string(uint32_t capacity);
// Finishes string creation by setting length, hash and interning it.
// Returns interned string or the same one.
ObjString *finish_new_string(ObjString *string, uint32_t length);
// Prevents GC from freeing it by adding to the list of `pinned_objects`.
void object_disable_gc(Object *object);
// Decrements pin count, when it hits 0 it's marked for removal from `pinned_objects` during GC.
void object_enable_gc(Object *object);

static inline bool is_object_type(Value value, ObjectType type) {
    return value.type == VAL_OBJECT && value.as.object->type == type;
}

#endif  // CLOX_OBJECT_H_
