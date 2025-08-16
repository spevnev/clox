#ifndef CLOX_HASHMAP_H_
#define CLOX_HASHMAP_H_

#include "value.h"

typedef struct ObjString ObjString;

typedef struct {
    // Key must be an interned string.
    ObjString *key;
    Value value;
} Entry;

// Linear probing hashmap
typedef struct {
    uint32_t count;
    uint32_t capacity;
    Entry *entries;
} HashMap;

uint32_t hash_string(const char *cstr, uint32_t length);
void free_hashmap(HashMap *map);
bool hashmap_get(HashMap *map, const ObjString *key, Value *value);
bool hashmap_set(HashMap *map, ObjString *key, Value value);
void hashmap_set_all(const HashMap *src, HashMap *dst);
bool hashmap_delete(HashMap *map, const ObjString *key);
ObjString *hashmap_find_key(HashMap *map, const char *cstr, uint32_t length, uint32_t hash);
void hashmap_mark_entries(HashMap *map);

#endif  // CLOX_HASHMAP_H_
