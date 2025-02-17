#include "compiler.h"
#include "common.h"
#include "lexer.h"

void compile(const char *source) {
    Lexer lexer = {0};
    init_lexer(&lexer, source);

    for (;;) {
        Token token = next_token(&lexer);
        printf("%2d '%.*s'\n", token.type, token.length, token.start);
        if (token.type == TOKEN_EOF) break;
    }
}
