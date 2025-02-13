#ifndef CLOX_ERROR_H_
#define CLOX_ERROR_H_

#include "common.h"

#define ERROR(...)                    \
    do {                              \
        fprintf(stderr, "[ERROR] ");  \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
        exit(EXIT_FAILURE);           \
    } while (0)

#define OUT_OF_MEMORY() ERROR("Program ran out of memory.")

#endif  // CLOX_ERROR_H_
