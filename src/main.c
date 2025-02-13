#include "chunk.h"
#include "common.h"
#include "debug.h"

int main(int argc, char **argv) {
    (void) argc, (void) argv;

    Chunk chunk = {0};

    chunk_push_byte(&chunk, OP_CONSTANT, 1);
    chunk_push_byte(&chunk, chunk_push_constant(&chunk, 2.2), 1);
    chunk_push_byte(&chunk, OP_CONSTANT, 2);
    chunk_push_byte(&chunk, chunk_push_constant(&chunk, 0), 2);

    disassemble_chunk(&chunk);
    chunk_free(&chunk);

    return EXIT_SUCCESS;
}
