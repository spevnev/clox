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

void free_object(Object *object) {
    switch (object->type) {
        case OBJ_STRING: reallocate(object, sizeof(ObjString) + ((ObjString *) object)->length + 1, 0); break;
        default:         UNREACHABLE();
    }
}

void print_object(const Object *object) {
    switch (object->type) {
        case OBJ_STRING: printf("%s", ((const ObjString *) object)->cstr); break;
        default:         UNREACHABLE();
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
