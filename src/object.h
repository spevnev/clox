#ifndef CLOX_OBJECT_H_
#define CLOX_OBJECT_H_

#include "chunk.h"
#include "common.h"
#include "native.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
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
    Chunk chunk;
} ObjFunction;

typedef struct {
    Object object;
    const char *name;
    int arity;
    NativeFun function;
} ObjNative;

void print_object(const Object *object);
void free_object(Object *object);
ObjFunction *new_function(void);
ObjNative *new_native(NativeDefinition def);
ObjString *copy_string(const char *cstr, uint32_t length);
ObjString *concat_strings(const ObjString *a, const ObjString *b);

#endif  // CLOX_OBJECT_H_
