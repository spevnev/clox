#ifndef CLOX_OBJECT_H_
#define CLOX_OBJECT_H_

#include "chunk.h"
#include "common.h"
#include "native.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_UPVALUE,
    OBJ_CLOSURE,
    OBJ_NATIVE,
} ObjectType;

typedef struct Object {
    ObjectType type;
    struct Object *next;
} Object;

typedef struct {
    Object object;
    uint32_t hash;
    uint32_t length;
    char cstr[];
} ObjString;

#define MAX_ARITY UINT8_MAX

typedef struct {
    Object object;
    ObjString *name;
    int arity;
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
    ObjUpvalue **upvalues;
    uint32_t upvalues_length;
} ObjClosure;

typedef struct {
    Object object;
    const char *name;
    int arity;
    NativeFun function;
} ObjNative;

void print_object(const Object *object);
void free_object(Object *object);
ObjFunction *new_function(void);
ObjUpvalue *new_upvalue(Value *value);
ObjClosure *new_closure(ObjFunction *function);
ObjNative *new_native(NativeDefinition def);
ObjString *copy_string(const char *cstr, uint32_t length);
ObjString *concat_strings(const ObjString *a, const ObjString *b);

#endif  // CLOX_OBJECT_H_
