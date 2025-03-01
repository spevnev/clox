#ifndef CLOX_COMPILER_H_
#define CLOX_COMPILER_H_

#include "common.h"
#include "object.h"

#define CONSTANTS_SIZE (UINT8_MAX + 1)
#define LOCALS_SIZE (UINT8_MAX + 1)
#define UPVALUES_SIZE (UINT8_MAX + 1)

ObjFunction *compile(const char *source);

#endif  // CLOX_COMPILER_H_
