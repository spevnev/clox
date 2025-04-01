#include "native.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "error.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static bool check_int_arg(Value arg, double min, double max) {
    if (arg.type != VAL_NUMBER) return false;

    double temp;
    double number = arg.as.number;
    return (min <= number && number <= max) && modf(number, &temp) == 0.0;
}

static bool clock_(Value *result, UNUSED(Value *args)) {
    *result = VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC);
    return true;
}

static bool has_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    Value unused;
    bool has_field = hashmap_get(&instance->fields, field, &unused);
    *result = VALUE_BOOL(has_field);
    return true;
}

static bool get_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    if (!hashmap_get(&instance->fields, field, result)) {
        runtime_error("Undefined field '%s'", field->cstr);
        return false;
    }
    return true;
}

static bool set_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    hashmap_set(&instance->fields, field, args[2]);
    *result = args[2];
    return true;
}

static bool delete_field(Value *result, Value *args) {
    if (!is_object_type(args[0], OBJ_INSTANCE)) {
        runtime_error("The first argument must be an instance");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    ObjInstance *instance = (ObjInstance *) args[0].as.object;
    ObjString *field = (ObjString *) args[1].as.object;

    hashmap_delete(&instance->fields, field);
    *result = VALUE_NIL();
    return true;
}

static bool create_server(Value *result, UNUSED(Value *args)) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        runtime_error("Unable to create server: %s", strerror(errno));
        return false;
    }

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    *result = VALUE_NUMBER(fd);
    return true;
}

static bool server_listen(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a server socket");
        return false;
    }
    if (!check_int_arg(args[1], 1, UINT16_MAX)) {
        runtime_error("The second argument is a port number, it must be an integer between 1 and 65535");
        return false;
    }

    int fd = (int) args[0].as.number;
    uint16_t port = (uint16_t) args[1].as.number;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        runtime_error("Error in bind: %s", strerror(errno));
        return false;
    }
    if (listen(fd, 64) != 0) {
        runtime_error("Error in listen: %s", strerror(errno));
        return false;
    }

    *result = VALUE_NIL();
    return true;
}

static bool server_accept(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a server socket");
        return false;
    }

    int server_fd = (int) args[0].as.number;
    int connection_fd = accept(server_fd, NULL, NULL);
    if (connection_fd == -1) {
        runtime_error("Error in accept: %s", strerror(errno));
        return false;
    }

    *result = VALUE_NUMBER(connection_fd);
    return true;
}

static bool read_(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a file descriptor");
        return false;
    }
    if (!check_int_arg(args[1], 0, SIZE_MAX)) {
        runtime_error("The second argument is length, it must be a positive integer.");
        return false;
    }

    int fd = (int) args[0].as.number;
    size_t length = (size_t) args[1].as.number;

    ObjString *string = create_new_string(length);
    ssize_t bytes = read(fd, string->cstr, length);
    if (bytes == -1) {
        runtime_error("Error in read: %s", strerror(errno));
        return false;
    }
    string->cstr[bytes] = '\0';
    // Shorten the string up to `bytes`.
    string->length = bytes;
    finish_new_string(string);

    *result = VALUE_OBJECT(string);
    return true;
}

static bool write_(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a file descriptor");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }

    int fd = (int) args[0].as.number;
    ObjString *string = (ObjString *) args[1].as.object;

    ssize_t bytes = write(fd, string->cstr, string->length);
    if (bytes == -1) {
        runtime_error("Error in write: %s", strerror(errno));
        return false;
    }

    *result = VALUE_NIL();
    return true;
}

static bool close_(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a file descriptor");
        return false;
    }

    int fd = (int) args[0].as.number;
    if (close(fd) == -1) {
        runtime_error("Unable to close: %s", strerror(errno));
        return false;
    }

    *result = VALUE_NIL();
    return true;
}

static NativeFunctionDef functions[] = {
    // clang-format off
    // name,         arity, function
    { "clock",        0,    clock_           },

    { "hasField",     2,    has_field        },
    { "getField",     2,    get_field        },
    { "setField",     3,    set_field        },
    { "deleteField",  2,    delete_field     },

    { "createServer", 0,    create_server    },
    { "listen",       2,    server_listen    },
    { "accept",       1,    server_accept    },
    { "read",         2,    read_            },
    { "write",        2,    write_           },
    { "close",        1,    close_           },
    // clang-format on
};

void create_native_functions(void) {
    for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++) {
        ObjString *name = copy_string(functions[i].name, strlen(functions[i].name));
        Value function = VALUE_OBJECT(new_native(functions[i]));
        hashmap_set(&vm.globals, name, function);
    }
}
