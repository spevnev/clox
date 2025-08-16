#ifndef CLOX_DEBUG_H_
#define CLOX_DEBUG_H_

#include "chunk.h"

uint32_t disassemble_instr(const Chunk *chunk, uint32_t offset);
void disassemble_chunk(const Chunk *chunk, const char *name);

#endif  // CLOX_DEBUG_H_
