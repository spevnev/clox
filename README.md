# clox

Compiler and bytecode VM for Lox from [Crafting Interpreters](https://craftinginterpreters.com/).

The implementation has additional features, mainly coroutines, which are not part of the standard Lox.

## Additional features

- coroutines
    ```js
    // Async functions return promise that has to be awaited.
    async fun constant(c) { return c; }
    print constant(5); // <Promise>
    print await constant(5); // 5

    // `sleep` to wait in coroutine (doesn't block other coroutines).
    async fun delayedPrint(t, v) {
        sleep(t);
        print v;
    }

    // `yield` to do computation in parts to avoid blocking other coroutines.
    async fun fib(n) {
        var a = 1;
        var b = 2;
        for (var i = 0;i < n;i++) {
            yield;
            var c = a + b;
            a = b;
            b = c;
        }
        return a;
    }

    // Native functions for TCP server (see `examples` folder).
    ```

- string interpolation
    ```js
    var a = 5;
    print "a is {a}";
    ```

- `switch` statement
    ```js
    switch (value) {
        case 0: print "zero";
        case 1: print "one";
        case 2: {
            print "t";
            print "w";
            print "o";
        }
        default: print "default";
    }
    ```

- `break` and `continue`

- conditional operator
    ```python
    print condition ? "true" : "false";
    ```

- post increment
    ```js
    var a = 5;
    print a++; // 5
    print a;   // 6
    ```

## Native functions

| Name         | Arguments            | Description |
|--------------|----------------------|-------------|
| clock        |                      | Returns number of seconds since the program started. |
| sleep        | duration_ms          | Puts coroutine to sleep for duration milliseconds. |
| hasField     | object, field        | Returns whether object has field. |
| getField     | object, field        | Returns field value or throws runtime error if the field doesn't exist. |
| setField     | object, field, value | Sets or overwrites the field. |
| deleteField  | object, field        | Deletes the field if it exists. |
| createServer |                      | Returns server socket that is an argument to other functions. |
| serverListen | server, port         | Starts listening on port or throws runtime error if the other process has already taken it. |
| serverAccept | server               | Returns a promise of client socket, that will be resolved when client connects to the server.  |
| socketRead   | socket, max length   | Return a promise of string of at most max length, that will be resolved when it reads from client. |
| socketWrite  | socket, string       | Returns a promise that will be resolved once the entirety of string has been written. |
| socketClose  | socket               | Closes client socket. |
