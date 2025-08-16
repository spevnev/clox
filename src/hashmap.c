#include "hashmap.h"
#include <assert.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"

#define MAX_LOAD 0.75
#define TOMBSTONE_VALUE_TYPE VAL_BOOL  // Any non-null value

static Entry *find_entry(Entry *entries, uint32_t capacity, const ObjString *key) {
    Entry *tombstone = NULL;
    for (uint32_t index = key->hash & (capacity - 1);; index = (index + 1) & (capacity - 1)) {
        Entry *entry = &entries[index];

        if (entry->key == key) {
            return entry;
        } else if (entry->key == NULL) {
            if (entry->value.type == TOMBSTONE_VALUE_TYPE) {
                if (tombstone == NULL) tombstone = entry;
            } else {
                return tombstone == NULL ? entry : tombstone;
            }
        }
    }
}

static void grow_map(HashMap *map) {
    uint32_t new_capacity = MAP_GROW_CAPACITY(map->capacity);
    Entry *new_entries = ARRAY_ALLOC(new_entries, new_capacity);

    static_assert(VAL_NIL == 0, "Nil type must be 0");
    memset(new_entries, 0, sizeof(*map->entries) * new_capacity);

    // Recount to exclude tombstones.
    map->count = 0;

    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key == NULL) continue;
        Entry *new_entry = find_entry(new_entries, new_capacity, map->entries[i].key);
        *new_entry = map->entries[i];
        map->count++;
    }

    ARRAY_FREE(map->entries, map->capacity);
    map->capacity = new_capacity;
    map->entries = new_entries;
}

// FNV-1a
uint32_t hash_string(const char *cstr, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t) cstr[i];
        hash *= 16777619;
    }
    return hash;
}

void free_hashmap(HashMap *map) {
    map->entries = ARRAY_FREE(map->entries, map->capacity);
    map->capacity = 0;
    map->count = 0;
}

bool hashmap_get(HashMap *map, const ObjString *key, Value *value) {
    if (map->count == 0) return false;

    Entry *entry = find_entry(map->entries, map->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

bool hashmap_set(HashMap *map, ObjString *key, Value value) {
#ifdef DEBUG_STRESS_GC
    collect_garbage();
#endif
    if (map->count >= map->capacity * MAX_LOAD) grow_map(map);

    Entry *entry = find_entry(map->entries, map->capacity, key);
    bool is_new = entry->key == NULL;
    if (is_new && entry->value.type != TOMBSTONE_VALUE_TYPE) map->count++;

    entry->key = key;
    entry->value = value;

    return is_new;
}

void hashmap_set_all(const HashMap *src, HashMap *dst) {
    for (uint32_t i = 0; i < src->capacity; i++) {
        if (src->entries[i].key == NULL) continue;
        hashmap_set(dst, src->entries[i].key, src->entries[i].value);
    }
}

bool hashmap_delete(HashMap *map, const ObjString *key) {
    if (map->count == 0) return false;

    Entry *entry = find_entry(map->entries, map->capacity, key);
    if (entry->key == NULL) return false;

    entry->key = NULL;
    entry->value.type = TOMBSTONE_VALUE_TYPE;
    return true;
}

ObjString *hashmap_find_key(HashMap *map, const char *cstr, uint32_t length, uint32_t hash) {
    if (map->count == 0) return NULL;

    for (uint32_t index = hash & (map->capacity - 1);; index = (index + 1) & (map->capacity - 1)) {
        ObjString *key = map->entries[index].key;

        if (key == NULL) {
            if (map->entries[index].value.type != TOMBSTONE_VALUE_TYPE) return NULL;
        } else if (key->hash == hash && key->length == length && memcmp(key->cstr, cstr, length) == 0) {
            return key;
        }
    }
}

void hashmap_mark_entries(HashMap *map) {
    for (uint32_t i = 0; i < map->capacity; i++) {
        Entry *entry = &map->entries[i];
        if (entry->key == NULL) continue;

        mark_object((Object *) entry->key);
        mark_value(&entry->value);
    }
}
