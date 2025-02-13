#include "memory.h"
#include "common.h"
#include "error.h"

void *reallocate(void *old_ptr, size_t old_size, size_t new_size) {
    (void) old_size;

    if (new_size == 0) {
        free(old_ptr);
        return NULL;
    }

    void *new_ptr = realloc(old_ptr, new_size);
    if (new_ptr == NULL) OUT_OF_MEMORY();

    return new_ptr;
}
