#ifndef CLOX_MEMORY_H_
#define CLOX_MEMORY_H_

#include "common.h"
#include "value.h"

#define GC_INITIAL_THRESHOLD (1024 * 1024)
#define GC_GROW_FACTOR 2

#define GROW_CAPACITY(capacity, initial, factor) ((capacity) == 0 ? initial : (capacity) * factor)

#define VEC_GROW_CAPACITY(capacity) GROW_CAPACITY((capacity), 16, 2)
#define GREY_GROW_CAPACITY(capacity) GROW_CAPACITY((capacity), 128, 2)

// Initial size and grow factor must be powers of 2
#define MAP_GROW_CAPACITY(capacity) GROW_CAPACITY((capacity), 64, 2)

#define ARRAY_ALLOC(array, capacity) ALLOC(sizeof((*array)) * (capacity))
#define ARRAY_REALLOC(array, old_capacity, new_capacity) \
    reallocate((array), sizeof((*array)) * (old_capacity), sizeof((*array)) * (new_capacity))
#define ARRAY_FREE(array, capacity) FREE((array), sizeof((*array)) * (capacity))

#define ALLOC(size) reallocate(NULL, 0, (size))
#define FREE(ptr, size) reallocate(ptr, (size), 0)

void *reallocate(void *ptr, size_t old_size, size_t new_size);
void mark_object(Object *object);
void mark_value(Value *value);
void collect_garbage(void);

#endif  // CLOX_MEMORY_H_
