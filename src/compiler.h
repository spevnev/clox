#ifndef CLOX_COMPILER_H_
#define CLOX_COMPILER_H_

#include "common.h"
#include "object.h"

ObjFunction *compile(const char *source);

#endif  // CLOX_COMPILER_H_
