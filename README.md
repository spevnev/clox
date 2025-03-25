# clox

Compiler and bytecode VM for Lox from [Crafting Interpreters](https://craftinginterpreters.com/).

## Additional features

- `break` and `continue`

- conditional operator (`?:`)
    ```python
    print condition ? "true" : "false";
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

- string interpolation
    ```js
    var a = 5;
    print "a is {a}";
    ```
