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
    ObjUpvalue **upvalues;
} ObjClosure;

typedef struct {
    Object object;
    const char *name;
    uint8_t arity;
    NativeFun function;
} ObjNative;

typedef struct {
    Object object;
    ObjString *name;
} ObjClass;

typedef struct {
    Object object;
    ObjClass *class;
    HashMap fields;
} ObjInstance;

static inline bool is_object_type(Value value, ObjectType type) {
    return value.type == VAL_OBJECT && value.as.object->type == type;
}

void print_object(const Object *object);
void free_object(Object *object);
ObjFunction *new_function(ObjString *name);
ObjUpvalue *new_upvalue(Value *value);
ObjClosure *new_closure(ObjFunction *function);
ObjNative *new_native(NativeDefinition def);
ObjClass *new_class(ObjString *name);
ObjInstance *new_instance(ObjClass *class);
ObjString *copy_string(const char *cstr, uint32_t length);
ObjString *concat_strings(const ObjString *a, const ObjString *b);

#endif  // CLOX_OBJECT_H_
