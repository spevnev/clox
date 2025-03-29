#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "vm.h"

static char *read_entire_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) PANIC("Unable to open file \"%s\": %s", path, strerror(errno));

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0L, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (buffer == NULL) OUT_OF_MEMORY();

    size_t bytes_read = fread(buffer, sizeof(char), size, file);
    if (bytes_read < size) {
        free(buffer);
        PANIC("Unable to read file \"%s\": %s", path, strerror(errno));
    }
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s        - REPL\n", program);
    fprintf(stderr, "  %s <path> - run file\n", program);
}

static void run_repl(void) {
    init_vm();

    size_t size;
    char *line = NULL;
    for (;;) {
        printf("> ");
        fflush(stdout);

        if (getline(&line, &size, stdin) == -1) {
            printf("\n");
            break;
        }

        interpret(line);
    }

    free_vm();
    free(line);
}

static int run_file(const char *path) {
    char *source = read_entire_file(path);

    init_vm();
    InterpretResult result = interpret(source);
    free_vm();

    free(source);

    switch (result) {
        case RESULT_OK:            return EXIT_SUCCESS;
        case RESULT_COMPILE_ERROR: return EX_DATAERR;
        case RESULT_RUNTIME_ERROR: return EXIT_FAILURE;
        default:                   UNREACHABLE();
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        run_repl();
        return EXIT_SUCCESS;
    } else if (argc == 2) {
        return run_file(argv[1]);
    } else {
        usage(argv[0]);
        return EX_USAGE;
    }
}
