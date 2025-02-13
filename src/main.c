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
    push_byte(&chunk, push_constant(&chunk, 2.2), 1);
    push_byte(&chunk, OP_RETURN, 2);

    interpret(&vm, &chunk);

    free_chunk(&chunk);
    free_vm(&vm);

    return EXIT_SUCCESS;
}
