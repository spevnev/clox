#ifndef CLOX_ERROR_H_
#define CLOX_ERROR_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "chunk.h"

static inline void error_varg(Loc loc, const char *fmt, va_list args) {
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, " at %u:%u.\n", loc.line, loc.column);
}

#define PANIC(...)                    \
    do {                              \
        fprintf(stderr, "[ERROR] ");  \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, ".\n");       \
        exit(EXIT_FAILURE);           \
    } while (0)

#define OUT_OF_MEMORY() PANIC("Program ran out of memory")
#define UNREACHABLE() PANIC("Unreachable")

#endif  // CLOX_ERROR_H_
