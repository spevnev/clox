#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

int main(int argc, char **argv) {
    (void) argc, (void) argv;

    VM vm = {0};
    init_vm(&vm);

    Chunk chunk = {0};
    push_byte(&chunk, OP_CONSTANT, 1);
    push_byte(&chunk, push_constant(&chunk, 1.2), 1);
    push_byte(&chunk, OP_CONSTANT, 2);
    push_byte(&chunk, push_constant(&chunk, 3.4), 2);
    push_byte(&chunk, OP_ADD, 3);
    push_byte(&chunk, OP_CONSTANT, 4);
    push_byte(&chunk, push_constant(&chunk, 5.6), 4);
    push_byte(&chunk, OP_DIVIDE, 5);
    push_byte(&chunk, OP_NEGATE, 6);
    push_byte(&chunk, OP_RETURN, 7);

    interpret(&vm, &chunk);

    free_chunk(&chunk);
    free_vm(&vm);

    return EXIT_SUCCESS;
}
