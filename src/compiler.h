#ifndef CLOX_COMPILER_H_
#define CLOX_COMPILER_H_

#include "object.h"

#define CONSTANTS_SIZE (MAX_OPERAND + 1)
#define LOCALS_SIZE (MAX_OPERAND + 1)
#define UPVALUES_SIZE (MAX_OPERAND + 1)

ObjFunction *compile(const char *source);
void mark_compiler_roots(void);

#endif  // CLOX_COMPILER_H_
