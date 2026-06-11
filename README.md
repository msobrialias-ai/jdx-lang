# JDX Language Reference

<img src="vsxode-extension/logo.png" width="200px" height="200px" />

JDX is a lightweight interpreted language with a JavaScript-like surface syntax and a compact runtime API.  
This document covers the current syntax, core language features, and the built-in `System` and `Develoment` objects provided by the runtime.

---

## 1. Getting Started

### Run a script

```bash
./jdx path/to/script.jdx arg1 arg2 arg3
```

At runtime, the command-line arguments are exposed through:

```jdx
System.Args
```

---

## 2. Language Overview

JDX supports:

- `let` and `const` variable declarations
- `fname` function declarations
- `class` declarations
- `if / elif / else`
- `while` and `for`
- `try / catch`
- `throw`, `return`, `break`, and `continue`
- module `import` and `export`
- object property access with dot notation
- function calls and method calls
- boolean, numeric, string, and null literals

### Lexical rules

- Comments:
  - Line comment: `// ...`
  - Block comment: `/* ... */`
- String literals support single or double quotes:
  - `'text'`
  - `"text"`
- Escape sequences:
  - `\n`, `\r`, `\t`, `\\`, `\'`, `\"`
- Numeric literals support integers and decimals:
  - `10`
  - `3.14`

---

## 3. Basic Syntax

### Variable declarations

```jdx
let name = "JDX";
const version = 1;
```

Rules:

- `let` creates a mutable binding.
- `const` creates an immutable binding.
- `const` must be initialized.

```jdx
const pi = 3.14159;
```

Invalid:

```jdx
const value;
```

### Assignment

```jdx
name = "Updated";
counter = counter + 1;
user.name = "Alice";
```

Assignments can target:

- variables
- object properties

---

## 4. Expressions and Operators

### Literals

```jdx
true
false
null
123
45.67
"hello"
```

### Arithmetic operators

- `+`
- `-`
- `*`
- `/`
- `%`

Example:

```jdx
let total = 10 + 5 * 2;
let remainder = 10 % 3;
```

### Comparison operators

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

Example:

```jdx
if (score >= 90) {
    System.Output("Excellent");
}
```

### Logical operators

- `&&`
- `||`
- `!`

Example:

```jdx
if (isAdmin && isActive) {
    System.Output("Access granted");
}
```

### String concatenation

The `+` operator concatenates when either operand is a string.

```jdx
let message = "Hello, " + name;
```

### Grouping

```jdx
let result = (2 + 3) * 4;
```

### Property access

```jdx
System.Output("Ready");
person.name;
```

### Property assignment

```jdx
person.name = "Jane";
```

---

## 5. Functions

### Function declaration

Use `fname` to declare a function.

```jdx
fname add(a, b) {
    return a + b;
}
```

### Function call

```jdx
let result = add(10, 20);
```

### Anonymous/default-export functions

A default export may use an unnamed function form:

```jdx
export default fname (a, b) {
    return a * b;
}
```

### Function rules

- Parameters are positional.
- The argument count must match exactly.
- `return` exits the function with a value.
- `return` at top level is not allowed.

### Closures

Functions capture the environment in which they are declared.

```jdx
fname makeAdder(x) {
    fname add(y) {
        return x + y;
    }
    return add;
}
```

---

## 6. Control Flow

### `if / elif / else`

```jdx
if (x > 0) {
    System.Output("positive");
} elif (x < 0) {
    System.Output("negative");
} else {
    System.Output("zero");
}
```

### `while`

```jdx
let i = 0;
while (i < 5) {
    System.Output(i);
    i = i + 1;
}
```

### `for`

The `for` loop supports the standard initializer, condition, and increment structure.

```jdx
for (let i = 0; i < 10; i = i + 1) {
    System.Output(i);
}
```

The initializer may also be an expression or a function declaration, and the condition/increment clauses are optional.

### `break` and `continue`

```jdx
for (let i = 0; i < 10; i = i + 1) {
    if (i == 3) {
        continue;
    }
    if (i == 8) {
        break;
    }
    System.Output(i);
}
```

---

## 7. Error Handling

### Throwing an error

```jdx
throw "Something went wrong";
```

### Try/catch

```jdx
try {
    throw "boom";
} catch (err) {
    System.Output(err.message);
}
```

### Error object shape

Thrown values are wrapped into an error-like object with a `message` field when caught by `catch`.

---

## 8. Classes

### Class declaration

```jdx
class Point {
    fname init(x, y) {
        this.x = x;
        this.y = y;
    }

    fname move(dx, dy) {
        this.x = this.x + dx;
        this.y = this.y + dy;
    }
}
```

### Instantiation

A class is called like a function:

```jdx
let p = Point(10, 20);
```

### Class behavior

- Class fields and methods are copied into each instance.
- If a method named `init` exists, it is used as the constructor.
- Constructor arguments are passed to `init`.
- `this` is only valid inside methods.

### Property access on instances

```jdx
System.Output(p.x);
p.move(1, 2);
```

---

## 9. Modules

JDX supports `import` and `export`.

### Import forms

#### Side-effect import

```jdx
import "jdx:utils";
```

#### Default import

```jdx
import Math from "jdx:math";
```

#### Named imports

```jdx
import { add, double as twice } from "jdx:math";
```

#### Namespace import

```jdx
import * as FS from "jdx:fs";
```

#### Default + named imports

```jdx
import Math, { add, double } from "jdx:math";
```

### Export forms

#### Export a binding list

```jdx
let value = 10;
fname helper() { return value; }

export { value, helper };
```

#### Export declarations directly

```jdx
export let answer = 42;

export const name = "JDX";

export fname add(a, b) {
    return a + b;
}

export class Tool {
}
```

#### Default export

```jdx
export default fname multiply(a, b) {
    return a * b;
}
```

```jdx
export default 123;
```

### Module resolution

The runtime resolves modules in this order:

1. Built-in internal modules such as `jdx:math`
2. Relative paths
3. Direct filesystem paths
4. External modules in `jdx_modules/`

### Built-in module namespace examples

- `jdx:math`
- `jdx:fs`
- `jdx:time`
- `jdx:regex`
- `jdx:utils`

---

## 10. Built-in Runtime API

The global environment always provides:

- `System`
- `Develoment`

> Note: `Develoment` is spelled exactly as in the runtime.

### `System`

#### `System.Args`
Array of command-line arguments passed after the script path.

#### Output and logging

- `System.Output(...)`
- `System.Log(message)`
- `System.Warn(message)`
- `System.Error(message)`

#### Filesystem

- `System.ReadFile(path)` → string
- `System.WriteFile(path, content)` → null
- `System.Exists(path)` → boolean
- `System.FileSystem()` → filesystem information object

#### Time

- `System.Clock()` → formatted UTC timestamp string
- `System.Time()` → Unix epoch milliseconds
- `System.Sleep(ms)` → pause execution

#### Miscellaneous utilities

- `System.Random()` → 64-bit random integer
- `System.Type(value)` → type name string
- `System.Len(value)` → length of string, array, or object
- `System.Lower(text)` → lowercase string
- `System.Upper(text)` → uppercase string
- `System.ShowSystemInfo()` → system information object
- `System.JGex(pattern)` → regex object
- `System.Regex(pattern)` → alias of `System.JGex(pattern)`
- `System.SafeExec(callable, ...args)` → safe execution wrapper
- `System.Server` → server/network namespace

### `System.FileSystem()`

Returns an object with common filesystem paths:

- `cwd`
- `home`
- `temp`
- `root`

Example:

```jdx
let fs = System.FileSystem();
System.Output(fs.cwd);
```

### `System.JGex(pattern)`

Creates a regular-expression-like object with methods such as:

- `test(input)`
- `match(input)`
- `search(input)`
- `replace(input, replacement)`
- `split(input)`

Example:

```jdx
let rx = System.JGex("^hello");
System.Output(rx.test("hello world"));
```

### `System.SafeExec(callable, ...args)`

Executes a callable and returns a structured result object:

- `ok`
- `error`
- `value`

This is useful for capturing runtime failures without aborting the program.

### `Develoment`

The `Develoment` namespace is intended for development-time features.

#### `Develoment.Stacktrace`

Properties:

- `Level`
- `Type`

These values influence stacktrace formatting and depth.

#### `Develoment.Test`

- `Assert(condition, message?)`
- `Equal(a, b, message?)`
- `Throws(callable, ...args)`

Example:

```jdx
Develoment.Test.Assert(1 == 1, "Math failed");
Develoment.Test.Equal("a", "a", "Strings differ");
```

---

## 11. Built-in Modules

### `jdx:math`

```jdx
import { add, double } from "jdx:math";

System.Output(add(2, 3));     // 5
System.Output(double(4));     // 8
```

Exports:

- `add(a, b)`
- `double(n)`
- default export: `multiply(a, b)`

### `jdx:fs`

Exports:

- `readFile(path)`
- `writeFile(path, content)`
- `exists(path)`
- `cwd()`
- `home()`
- `temp()`
- `root()`

### `jdx:time`

Exports:

- `now()` → formatted UTC time string
- `now_ms()` → epoch milliseconds

### `jdx:regex`

Exports:

- `Compile(pattern)`
- `Test(pattern, input)`
- `Match(pattern, input)`
- `Search(pattern, input)`
- `Replace(pattern, input, replacement)`
- `Split(pattern, input)`

### `jdx:utils`

Exports:

- `Print(message)`
- `Log(message)`
- `Warn(message)`
- `Error(message)`
- `Sleep(ms)`
- `Random()`
- `Type(input)`
- `Length(str)`
- `SystemInfo()`
- `Lower(input)`
- `Upper(input)`
- `Buffer()`
- `Regex(pattern)`
- `RegexTest(pattern, input)`
- `RegexMatch(pattern, input)`
- `RegexSearch(pattern, input)`
- `RegexReplace(pattern, input, replacement)`
- `RegexSplit(pattern, input)`

---

## 12. Operator Precedence

From higher to lower precedence:

1. Unary: `!`, `-`
2. Multiplicative: `*`, `/`, `%`
3. Additive: `+`, `-`
4. Comparison: `<`, `<=`, `>`, `>=`
5. Equality: `==`, `!=`
6. Logical AND: `&&`
7. Logical OR: `||`
8. Assignment: `=`

---

## 13. Common Examples

### Hello world

```jdx
System.Output("Hello, world!");
```

### Simple module export

```jdx
export fname square(n) {
    return n * n;
}
```

### Import and use

```jdx
import { square } from "jdx:math";

System.Output(square(9));
```

### Try/catch with safe handling

```jdx
try {
    System.ReadFile("missing.txt");
} catch (err) {
    System.Warn(err.message);
}
```

---

## 14. Notes

- Semicolons are required after most statements.
- The runtime uses strict argument counts for user-defined functions and constructors.
- Objects are dynamic; missing properties raise runtime errors on access.

---

## 15. Minimal Example

```jdx
import { add } from "jdx:math";

let name = "JDX";
const a = 10;
const b = 20;

fname greet(person) {
    return "Hello, " + person;
}

class Greeter {
    fname init(prefix) {
        this.prefix = prefix;
    }

    fname say(person) {
        return this.prefix + ", " + person;
    }
}

let g = Greeter("Welcome");
System.Output(greet(name));
System.Output(add(a, b));
System.Output(g.say("world"));
```
