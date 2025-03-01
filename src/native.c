#include "native.h"
#include <assert.h>
#include <time.h>
#include "common.h"
#include "value.h"

static Value clock_fun(UNUSED(Value* args), uint8_t arg_num) {
    assert(arg_num == 0);
    return VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC);
}

NativeDefinition native_defs[] = {
    // clang-format off
    // name,   arity, function
    { "clock", 0,     clock_fun },
    // clang-format on
};

size_t native_defs_length() { return sizeof(native_defs) / sizeof(NativeDefinition); }
