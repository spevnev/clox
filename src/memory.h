#ifndef CLOX_MEMORY_H_
#define CLOX_MEMORY_H_

#include "common.h"

#define GROW_CAPACITY(capacity, initial, factor) ((capacity) == 0 ? initial : (capacity) * factor)

#define VEC_GROW_CAPACITY(capacity) GROW_CAPACITY((capacity), 8, 2)
#define MAP_GROW_CAPACITY(capacity) GROW_CAPACITY((capacity), 64, 2)

#define ARRAY_REALLOC(array, old_capacity, new_capacity) \
    reallocate((array), sizeof((*array)) * (old_capacity), sizeof((*array)) * (new_capacity))
#define ARRAY_FREE(array, capacity) reallocate((array), sizeof((*array)) * (capacity), 0)

void *reallocate(void *ptr, size_t old_size, size_t new_size);

#endif  // CLOX_MEMORY_H_
