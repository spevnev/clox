#ifndef CLOX_MEMORY_H_
#define CLOX_MEMORY_H_

#include "common.h"

#define VEC_INITIAL_CAPACITY 8
#define VEC_GROWTH_FACTOR 2

#define VEC_GROW_CAPACITY(capacity) ((capacity) == 0 ? VEC_INITIAL_CAPACITY : (capacity) * VEC_GROWTH_FACTOR)

#define VEC_REALLOC(array, old_capacity, new_capacity) \
    reallocate((array), sizeof((*array)) * (old_capacity), sizeof((*array)) * (new_capacity))
#define VEC_FREE(array, capacity) reallocate((array), sizeof((*array)) * (capacity), 0)

void *reallocate(void *ptr, size_t old_size, size_t new_size);

#endif  // CLOX_MEMORY_H_
