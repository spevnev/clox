#ifndef CLOX_ERROR_H_
#define CLOX_ERROR_H_

#include "common.h"

#define ERROR(...)                    \
    do {                              \
        fprintf(stderr, "[ERROR] ");  \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, ".\n");       \
    } while (0)

#define ERROR_AT(line, ...)                      \
    do {                                         \
        fprintf(stderr, "[ERROR] ");             \
        fprintf(stderr, __VA_ARGS__);            \
        fprintf(stderr, " at line %d.\n", line); \
    } while (0)

#define PANIC(...)          \
    do {                    \
        ERROR(__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (0)

#define OUT_OF_MEMORY() PANIC("Program ran out of memory")
#define UNREACHABLE() PANIC("Unreachable")

#endif  // CLOX_ERROR_H_
