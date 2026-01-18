# How to Code in Splice

Splice is a lightweight and embeddable programming language.
Its syntax is inspired by Lua and Python, with additional custom syntax
designed to keep the language simple and predictable.

Splice programs are executed from top to bottom.

---

## Program Structure

A basic Splice program consists of statements separated by semicolons.

```splice
print("Hello, Splice");
```

---

## Comments

Splice supports single-line comments using `//`.

```splice
// This is a comment
print("This line will execute");
```

---

## Printing Output

Printing output is done using the `print` keyword.

```splice
print("Hi");
```

### Printing Variables

```splice
let x = 10;
print(x);
```

---

## Variables

Variables are declared using the `let` keyword.

```splice
let number = 42;
let name = "Splice";
let pi = 3.14;
```

Splice is dynamically typed, meaning the type is determined at runtime.

---

## Arithmetic Operations

Splice supports basic arithmetic operations.

```splice
let a = 10;
let b = 5;

print(a + b);
print(a - b);
print(a * b);
print(a / b);
```

---

## Comparison Operators

Splice supports standard comparison operators.

```splice
let x = 10;

print(x == 10);
print(x != 5);
print(x > 5);
print(x < 20);
```

---

## If Statements

Conditional execution is done using `if`.

```splice
let age = 18;

if (age >= 18) {
    print("You are an adult");
}
```

---

## If-Else Statements

```splice
let score = 40;

if (score >= 50) {
    print("Pass");
} else {
    print("Fail");
}
```

---

## While Loops

The `while` loop executes as long as the condition is true.

```splice
let i = 0;

while (i < 5) {
    print(i);
    i = i + 1;
}
```

---

## For Loops

Splice supports `for` loops with a counter variable.

```splice
for (let i = 0; i < 5; i = i + 1) {
    print(i);
}
```

---

## Functions

Functions are declared using the `func` keyword.

```splice
func add(a, b) {
    return a + b;
}
```

### Calling Functions

```splice
let result = add(10, 20);
print(result);
```

---

## Return Statement

Functions return values using `return`.

```splice
func square(x) {
    return x * x;
}

print(square(5));
```

---

## User Input

Splice supports input using the `input` keyword.

```splice
let name = input("Enter your name: ");
print(name);
```

---

## Error Handling

Splice provides basic error reporting at runtime.
Errors include:

- Undefined variables
- Invalid operations
- Syntax errors

---

## Execution Model (High Level)

Splice executes programs by walking an Abstract Syntax Tree (AST).
Each node represents a language construct such as variables, function calls,
loops, or expressions.

This design allows:

- Easy embedding in C programs
- Simpler debugging
- Low memory overhead

---

## Embedding Splice in C

Splice is designed to be embedded in C applications.

```c
#include <Arduino.h>

#define ARDUINO
#define SPLICE_EMBED 1

#include "splice.h"

/*
Splice source (compiled beforehand):

print("Hello from Splice");
*/

const unsigned char program[] = {
    'S','P','C',0x00,   // magic
    0x01,              // version

    AST_PRINT,
      AST_STRING,
        0x00, 0x11,
        'H','e','l','l','o',' ',
        'f','r','o','m',' ',
        'S','p','l','i','c','e'
};

void setup() {
    Serial.begin(115200);
    while (!Serial);

    ASTNode *root = read_ast_from_spc_mem(
        program,
        sizeof(program)
    );

    interpret(root);
}

void loop() {}

```

---


