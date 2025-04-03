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
    OBJ_SOCKET,
} ObjectType;

typedef struct Object {
    bool is_marked;
    ObjectType type;
    struct Object *next;
} Object;

typedef struct ObjString {
    Object object;
    uint32_t hash;
    uint32_t length;
    char cstr[];
} ObjString;

#define MAX_ARITY UINT8_MAX

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
    int fd;
} ObjSocket;

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
ObjSocket *new_socket(int fd);
ObjString *copy_string(const char *cstr, uint32_t length);
ObjString *concat_strings(const ObjString *a, const ObjString *b);

// Create a new string of the given length for callee to fill `cstr`.
// After filling in the entire length, callee must `finish_new_string`.
ObjString *create_new_string(uint32_t length);
// Finishes string creation by setting hash and interning it.
// Returns interned string or the same one.
ObjString *finish_new_string(ObjString *string);

static inline bool is_object_type(Value value, ObjectType type) {
    return value.type == VAL_OBJECT && value.as.object->type == type;
}

#endif  // CLOX_OBJECT_H_
