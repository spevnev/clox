#ifndef CLOX_COMMON_H_
#define CLOX_COMMON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNUSED(parameter) __attribute__((unused)) parameter##_UNUSED

// #define DEBUG_PRINT_BYTECODE
// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#define INLINE_CACHING

#endif  // CLOX_COMMON_H_
