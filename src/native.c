#include "native.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "error.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static bool clock_(Value *result, UNUSED(Value *args)) {
    *result = VALUE_NUMBER((double) clock() / CLOCKS_PER_SEC);
    return true;
}

static bool sleep_(Value *result, Value *args) {
    if (args[0].type != VAL_NUMBER || args[0].as.number < 0) {
        runtime_error("The first argument is number of milliseconds, it must be a positive number");
        return false;
    }
    double duration_ms = args[0].as.number;

    Coroutine *sleeping = ll_remove(&vm.active_head, &vm.coroutine);
    sleeping->sleep_time_ms = get_time_ms() + duration_ms;
    ll_add_head(&vm.sleeping_head, sleeping);

    if (vm.coroutine == NULL && schedule_coroutine() == RESULT_RUNTIME_ERROR) return false;

    *result = VALUE_NIL();
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
        runtime_error("Error in socket (%s)", strerror(errno));
        return false;
    }

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        runtime_error("Error in fcntl (%s)", strerror(errno));
        return false;
    }

    *result = VALUE_NUMBER(fd);
    return true;
}

static bool server_listen(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a server");
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
        if (errno == EADDRINUSE) {
            runtime_error("Error in serverListen: the port is already taken");
        } else {
            runtime_error("Error in bind (%s)", strerror(errno));
        }
        return false;
    }
    if (listen(fd, 64) != 0) {
        runtime_error("Error in listen (%s)", strerror(errno));
        return false;
    }

    *result = VALUE_NIL();
    return true;
}

typedef struct {
    ObjPromise *promise;
} ServerAcceptData;

static bool server_accept_callback(EpollData *epoll_data) {
    ServerAcceptData *data = (void *) epoll_data->data;

    int client_fd = accept(epoll_data->fd, NULL, NULL);
    if (client_fd == -1) {
        if (errno == EAGAIN) return true;
        runtime_error("Error in accept (%s)", strerror(errno));
        return false;
    }
    if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
        runtime_error("Error in fcntl (%s)", strerror(errno));
        return false;
    }

    fulfill_promise(data->promise, VALUE_NUMBER(client_fd));
    object_enable_gc((Object *) data->promise);
    vm_epoll_delete(epoll_data);
    return true;
}

static bool server_accept(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT32_MAX)) {
        runtime_error("The first argument must be a server");
        return false;
    }
    int server_fd = (int) args[0].as.number;

    ObjPromise *promise = new_promise();
    *result = VALUE_OBJECT(promise);

    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd != -1) {
        fulfill_promise(promise, VALUE_NUMBER(client_fd));
        return true;
    }

    ServerAcceptData *data = vm_epoll_add(server_fd, EPOLLIN, &server_accept_callback, sizeof(ServerAcceptData));
    data->promise = promise;

    object_disable_gc((Object *) promise);
    return true;
}

typedef struct {
    size_t length;
    ObjString *string;
    ObjPromise *promise;
} SocketReadData;

static bool socket_read_callback(EpollData *epoll_data) {
    SocketReadData *data = (void *) epoll_data->data;

    ssize_t bytes = read(epoll_data->fd, data->string->cstr, data->length);
    if (bytes == -1) {
        if (errno == EAGAIN) return true;

        runtime_error("Error in read (%s)", strerror(errno));
        return false;
    }

    if (bytes == 0) {
        // Return nil on closed connection.
        fulfill_promise(data->promise, VALUE_NIL());
    } else {
        fulfill_promise(data->promise, VALUE_OBJECT(finish_new_string(data->string, bytes)));
    }

    object_enable_gc((Object *) data->promise);
    object_enable_gc((Object *) data->string);
    vm_epoll_delete(epoll_data);
    return true;
}

static bool socket_read(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT_MAX)) {
        runtime_error("The first argument must be a socket");
        return false;
    }
    if (!check_int_arg(args[1], 1, SIZE_MAX)) {
        runtime_error("The second argument is length, it must be a positive integer.");
        return false;
    }
    int fd = (int) args[0].as.number;
    size_t length = (size_t) args[1].as.number;

    ObjPromise *promise = new_promise();
    object_disable_gc((Object *) promise);
    *result = VALUE_OBJECT(promise);

    ObjString *string = create_new_string(length);
    ssize_t bytes = read(fd, string->cstr, length);
    if (bytes != -1) {
        if (bytes == 0) {
            // Return nil on closed connection.
            fulfill_promise(promise, VALUE_NIL());
        } else {
            fulfill_promise(promise, VALUE_OBJECT(finish_new_string(string, bytes)));
        }
        object_enable_gc((Object *) promise);
        return true;
    }

    SocketReadData *data = vm_epoll_add(fd, EPOLLIN, &socket_read_callback, sizeof(SocketReadData));
    data->length = length;
    data->string = string;
    data->promise = promise;

    object_disable_gc((Object *) string);
    return true;
}

typedef struct {
    ObjString *string;
    uint32_t offset;
    ObjPromise *promise;
} SocketWriteData;

static bool socket_write_callback(EpollData *epoll_data) {
    SocketWriteData *data = (void *) epoll_data->data;

    uint32_t length = data->string->length - data->offset;
    ssize_t bytes = write(epoll_data->fd, data->string->cstr + data->offset, length);
    if (bytes == -1) {
        if (errno == EAGAIN) return true;
        runtime_error("Error in write (%s)", strerror(errno));
        return false;
    }
    if (bytes < length) {
        data->offset += bytes;
        return true;
    }

    fulfill_promise(data->promise, VALUE_NIL());
    object_enable_gc((Object *) data->promise);
    object_enable_gc((Object *) data->string);
    vm_epoll_delete(epoll_data);
    return true;
}

static bool socket_write(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT_MAX)) {
        runtime_error("The first argument must be a socket");
        return false;
    }
    if (!is_object_type(args[1], OBJ_STRING)) {
        runtime_error("The second argument must be a string");
        return false;
    }
    int fd = (int) args[0].as.number;
    ObjString *string = (ObjString *) args[1].as.object;

    ObjPromise *promise = new_promise();
    *result = VALUE_OBJECT(promise);

    ssize_t bytes = write(fd, string->cstr, string->length);
    // Ignore EAGAIN and setup epoll as if nothing was written.
    if (bytes == -1 && errno == EAGAIN) bytes = 0;
    if (bytes == -1) {
        runtime_error("Error in write (%s)", strerror(errno));
        return false;
    }
    if (bytes == string->length) {
        fulfill_promise(promise, VALUE_NIL());
        return true;
    }

    SocketWriteData *data = vm_epoll_add(fd, EPOLLOUT, &socket_write_callback, sizeof(SocketWriteData));
    data->offset = bytes;
    data->string = string;
    data->promise = promise;

    object_disable_gc((Object *) promise);
    object_disable_gc((Object *) string);
    return true;
}

static bool socket_close(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, INT_MAX)) {
        runtime_error("The first argument must be a socket");
        return false;
    }
    int fd = (int) args[0].as.number;

    // https://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
    // Read pending data before closing to avoid sending RST.
    shutdown(fd, SHUT_WR);
    static char buffer[4096];
    while (read(fd, buffer, sizeof(buffer) / sizeof(*buffer)) > 0);
    close(fd);

    *result = VALUE_NIL();
    return true;
}

static bool create_array(Value *result, Value *args) {
    if (!check_int_arg(args[0], 0, UINT32_MAX)) {
        runtime_error("The first argument is length, it must be a non-negative integer");
        return false;
    }
    uint32_t size = (uint32_t) args[0].as.number;

    *result = VALUE_OBJECT(new_array(size, args[1]));
    return true;
}

static bool length(Value *result, Value *args) {
    if (is_object_type(args[0], OBJ_STRING)) {
        ObjString *string = (ObjString *) args[0].as.object;
        *result = VALUE_NUMBER(string->length);
        return true;
    } else if (is_object_type(args[0], OBJ_ARRAY)) {
        ObjArray *array = (ObjArray *) args[0].as.object;
        *result = VALUE_NUMBER(array->length);
        return true;
    } else {
        runtime_error("The first argument must be an array or a string");
        return false;
    }
}

static NativeFunctionDef functions[] = {
    // clang-format off
    // time
    { "clock",         0,     clock_        },
    { "sleep",         1,     sleep_        },
    // instance
    { "hasField",      2,     has_field     },
    { "getField",      2,     get_field     },
    { "setField",      3,     set_field     },
    { "deleteField",   2,     delete_field  },
    // net
    { "createServer",  0,     create_server },
    { "serverListen",  2,     server_listen },
    { "serverAccept",  1,     server_accept },
    { "socketRead",    2,     socket_read   },
    { "socketWrite",   2,     socket_write  },
    { "socketClose",   1,     socket_close  },
    // array
    { "Array",         2,     create_array  },
    { "length",        1,     length        },
    // clang-format on
};

void add_native_functions(void) {
    for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++) {
        ObjString *name = copy_string(functions[i].name, strlen(functions[i].name));
        Value function = VALUE_OBJECT(new_native(functions[i]));
        hashmap_set(&vm.globals, name, function);
    }
}
