# JDX Language Reference

## Overview

JDX is a lightweight, ECMAScript-inspired scripting language implemented by the JDX runtime. It is designed for practical automation, scripting, module composition, and native runtime integration.

The language supports:

- variable declarations
- function declarations
- control flow statements
- arithmetic and comparison expressions
- module import and export
- native runtime access through `System`
- built-in utility modules such as `jdx:math`, `jdx:fs`, `jdx:time`, `jdx:utils`, and `jdx:regex`

This document describes the syntax, grammar, runtime behavior, and built-in APIs available in the codebase.

---

## Table of Contents

- [Lexical Structure](#lexical-structure)
- [Tokens and Operators](#tokens-and-operators)
- [Grammar Overview](#grammar-overview)
- [Statements](#statements)
- [Expressions](#expressions)
- [Module System](#module-system)
- [Runtime Values](#runtime-values)
- [Global Native API: System](#global-native-api-system)
- [Built-in Modules](#built-in-modules)
- [Examples](#examples)
- [Implementation Notes](#implementation-notes)

---

## Lexical Structure

### Whitespace

Whitespace is ignored by the lexer and parser:

- spaces
- tabs
- carriage returns
- newlines

### Comments

JDX supports both line and block comments.

```jdx
// single-line comment

/*
  block comment
*/
```

### String Literals

Strings may be written using either double quotes or single quotes:

```jdx
"hello"
'world'
```

Supported escape sequences include:

- `\n`
- `\t`
- `\r`
- `\"`
- `\'`
- `\\`

Any other escape sequence is treated as a literal character after the backslash handling performed by the lexer.

### Numeric Literals

The lexer recognizes numeric literals in the following forms:

- integer literals, for example `123`
- floating-point literals, for example `12.34`

### Identifiers

Identifiers begin with a letter or underscore and may continue with letters, digits, or underscores.

Examples:

```jdx
name
user_name
_value1
```

---

## Tokens and Operators

### Keywords

The parser recognizes the following keywords:

- `let`
- `const`
- `return`
- `if`
- `elif`
- `else`
- `while`
- `for`
- `break`
- `continue`
- `function`
- `fname`
- `import`
- `export`
- `default`
- `from`
- `as`
- `NamedExport`
- `true`
- `false`
- `null`
- `System`

### Operators and Punctuation

The following operators and delimiters are supported:

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Logical negation: `!`
- Assignment: `=`
- Equality: `==`, `!=`
- Comparison: `<`, `<=`, `>`, `>=`
- Member access: `.`
- Separators: `,`, `:`, `;`
- Grouping and blocks: `(`, `)`, `{`, `}`

### Operator Precedence

From highest to lowest precedence, the parser evaluates expressions as follows:

1. function call and member access
2. unary operators: `!`, `-`
3. multiplicative operators: `*`, `/`, `%`
4. additive operators: `+`, `-`
5. comparison operators: `<`, `<=`, `>`, `>=`
6. equality operators: `==`, `!=`
7. assignment: `=`

---

## Grammar Overview

The following grammar is a structured representation of the parser behavior.

```ebnf
program
  = declaration* EOF ;

declaration
  = importDeclaration
  | exportDeclaration
  | variableDeclaration
  | functionDeclaration
  | namedExportDeclaration
  | statement ;
```

---

## Statements

### Variable Declarations

Variables are declared using `let` or `const`.

```ebnf
variableDeclaration
  = ("let" | "const") Identifier ("=" expression)? ";" ;
```

#### Examples

```jdx
let a = 10;
let b;
const c = "hello";
```

#### Notes

- `let` declarations may omit an initializer.
- `const` declarations require an initializer at runtime.
- Assigning to a constant after initialization will fail during evaluation.

---

### Function Declarations

Functions may be declared using either `function` or `fname`.

```ebnf
functionDeclaration
  = ("function" | "fname") Identifier? "(" parameterList? ")" block ;
```

#### Examples

```jdx
function add(a, b) {
  return a + b;
}

fname mul(a, b) {
  return a * b;
}
```

#### Notes

- Function bodies must be blocks.
- Parameters are comma-separated identifiers.
- The parser supports optional function names in certain export contexts.

---

### Block Statements

A block is a sequence of declarations enclosed in braces.

```ebnf
block
  = "{" declaration* "}" ;
```

#### Example

```jdx
{
  let x = 1;
  let y = 2;
}
```

---

### Conditional Statements

JDX supports `if`, `elif`, and `else`.

```ebnf
ifStatement
  = "if" "(" expression ")" statement
    ("elif" "(" expression ")" statement)*
    ("else" statement)? ;
```

#### Example

```jdx
if (x > 10) {
  Print("large");
} elif (x > 5) {
  Print("medium");
} else {
  Print("small");
}
```

---

### While Loops

```ebnf
whileStatement
  = "while" "(" expression ")" statement ;
```

#### Example

```jdx
while (i < 10) {
  i = i + 1;
}
```

---

### For Loops

```ebnf
forStatement
  = "for" "(" forInitializer? expression? ";" expression? ")" statement ;

forInitializer
  = variableDeclaration
  | expressionStatement
  | ";" ;
```

#### Example

```jdx
for (let i = 0; i < 10; i = i + 1) {
  Print(i);
}
```

#### Notes

- The initializer may be omitted.
- The condition is optional.
- The increment expression is optional.

---

### Return Statements

```ebnf
returnStatement
  = "return" expression? ";" ;
```

#### Examples

```jdx
return 42;
return;
```

---

### Break and Continue

```ebnf
breakStatement
  = "break" ";" ;

continueStatement
  = "continue" ";" ;
```

#### Examples

```jdx
break;
continue;
```

---

### Expression Statements

Expressions may be used as statements when terminated by a semicolon.

```ebnf
expressionStatement
  = expression ";" ;
```

#### Example

```jdx
Print("hello");
x = x + 1;
```

---

## Expressions

### Assignment

```ebnf
expression
  = assignment ;

assignment
  = equality ("=" assignment)? ;
```

#### Valid Assignment Targets

The parser accepts assignment targets such as:

- identifiers
- member access expressions, such as `object.property`

#### Example

```jdx
x = 10;
user.name = "Sobri";
```

---

### Equality and Comparison

```ebnf
equality
  = comparison (("==" | "!=") comparison)* ;

comparison
  = term ((">" | ">=" | "<" | "<=") term)* ;
```

#### Example

```jdx
a == b;
a != b;
a > b;
a <= b;
```

#### Runtime Behavior

- `==` and `!=` compare the stringified representation of both operands.
- `<`, `<=`, `>`, and `>=` coerce operands to numeric values using the runtime numeric conversion.

---

### Arithmetic Expressions

```ebnf
term
  = factor (("+" | "-") factor)* ;

factor
  = unary (("*" | "/" | "%") unary)* ;
```

#### Example

```jdx
1 + 2 * 3;
10 % 3;
```

#### Runtime Behavior

- `+` performs string concatenation if either operand is a string.
- Otherwise, arithmetic operators require numeric values.
- Division or modulo by zero raises a runtime error.

---

### Unary Expressions

```ebnf
unary
  = ("!" | "-") unary
  | call ;
```

#### Example

```jdx
!true;
-123;
```

---

### Call and Member Access

```ebnf
call
  = primary ( "(" argumentList? ")" | "." propertyName )* ;
```

#### Examples

```jdx
Print("hello");
object.method();
object.value;
```

#### Notes

- Calls may be chained.
- Member access uses the dot operator.
- Member assignment is supported when the left-hand side is a valid property access expression.

---

### Primary Expressions

```ebnf
primary
  = literal
  | Identifier
  | "System"
  | "import" "(" expression ")"
  | "(" expression ")" ;
```

#### Examples

```jdx
true
false
null
"hello"
123
someVar
System
import("jdx:math")
```

---

### Literals

```ebnf
literal
  = Number
  | String
  | "true"
  | "false"
  | "null" ;
```

---

### Argument Lists

```ebnf
argumentList
  = expression ("," expression)* ;
```

#### Example

```jdx
fn(1, 2, 3);
```

---

## Module System

JDX supports both static import declarations and dynamic import expressions.

---

### Import Declarations

#### Side-Effect Import

```jdx
import "jdx:utils";
```

#### Namespace Import

```jdx
import * as fs from "jdx:fs";
```

#### Default Import

```jdx
import multiply from "jdx:math";
```

#### Named Imports

```jdx
import { add, double } from "jdx:math";
import { readFile as rf, exists } from "jdx:fs";
```

#### Default + Named Imports

```jdx
import multiply, { add, double } from "jdx:math";
```

#### Grammar

```ebnf
importDeclaration
  = "import" STRING ";"
  | "import" "*" "as" Identifier "from" STRING ";"
  | "import" importClause "from" STRING ";" ;

importClause
  = Identifier ("," ("*" "as" Identifier | "{" importBindings "}"))?
  | "{" importBindings "}" ;

importBindings
  = importBinding ("," importBinding)* ;

importBinding
  = Identifier ("as" Identifier)? ;
```

#### Notes

- Module specifiers must be string literals.
- Import declarations must terminate with `;`.
- Default imports and namespace imports are mutually exclusive.

---

### Export Declarations

#### Export Variables, Constants, and Functions

```jdx
export let x = 1;
export const y = 2;
export function add(a, b) { return a + b; }
```

#### Default Export of an Expression

```jdx
export default 42;
export default "hello";
```

#### Default Export of a Function

```jdx
export default function multiply(a, b) {
  return a * b;
}
```

#### Named Export List

```jdx
export { internal as answer, inc };
NamedExport { internal as answer, inc };
```

#### Grammar

```ebnf
exportDeclaration
  = "export" "default" (functionDeclaration | expression ";")
  | "export" variableDeclaration
  | "export" functionDeclaration
  | "export" namedExportDeclaration ;

namedExportDeclaration
  = ("NamedExport" | "{") exportBindings "}" ";" ;

exportBindings
  = exportBinding ("," exportBinding)* ;

exportBinding
  = Identifier ("as" Identifier)? ;
```

#### Notes

- `export default` with an expression must end in `;`
- Named export lists must end in `;`
- The `as` keyword is used for aliases

---

### Dynamic Import Expression

JDX also supports runtime module loading through an import expression.

```jdx
let math = import("jdx:math");
```

#### Grammar

```ebnf
importExpression
  = "import" "(" expression ")" ;
```

#### Runtime Behavior

- The argument must resolve to a string value.
- The result is a module object.
- Loaded modules are cached after the first resolution.

---

### Module Resolution

The resolver searches in the following order:

1. Built-in modules using the `jdx:` prefix
   - `./src/modules/<name>.jdx`
   - `./src/modules/<name>/index.jdx`
2. Relative paths from the current file
3. Direct filesystem paths
4. `./jdx_modules/<specifier>/index.jdx`
5. `./jdx_modules/<specifier>.jdx`

---

## Runtime Values

The runtime supports the following value categories:

- `null`
- `bool`
- `number` (`int64` or `double`)
- `string`
- `array`
- `object`
- `function`

### Truthiness

The following values are considered false:

- `null`
- `false`
- `0`
- `0.0`
- empty string
- empty array
- empty object

All other values are considered true.

### String Conversion

Runtime string conversion behaves as follows:

- `null` → `"null"`
- `bool` → `"true"` or `"false"`
- `number` → numeric representation
- `string` → itself
- `array` → formatted array representation
- `object` → formatted object representation
- `function` → `"<native function>"` or `"<function>"`

### Type Identification

The runtime type system reports the following names:

- `null`
- `bool`
- `number`
- `string`
- `array`
- `object`
- `function`

---

## Global Native API: `System`

`System` is the primary native object available in the global scope.

---

### Input and Output

#### `System.Output(...values)`

Writes values to standard output, separated by spaces, followed by a newline.

#### `System.Input()`

Reads a single line from standard input and returns it as a string.

#### `System.Log(...values)`

Writes values to the logging stream.

#### `System.Warn(...values)`

Writes values to standard error.

#### `System.Error(...values)`

Writes values to standard error.

---

### Time and Execution Control

#### `System.Time()`

Returns the Unix timestamp in milliseconds.

#### `System.Clock()`

Returns the local time as a formatted string:

```text
YYYY-MM-DD HH:MM:SS
```

#### `System.Sleep(ms)`

Suspends execution for the specified number of milliseconds.

---

### Value Utilities

#### `System.Type(value)`

Returns the runtime type name of the supplied value.

#### `System.Len(value)`

Returns:

- string length for strings
- element count for arrays
- property count for objects
- otherwise, the length of the stringified representation

#### `System.Upper(value)`

Returns the uppercase string representation.

#### `System.Lower(value)`

Returns the lowercase string representation.

#### `System.Trim(value)`

Returns the trimmed string representation.

#### `System.SocketError()`

Returns the most recent socket error message as a string.

---

### Environment and Runtime Context

#### `System.Random()`

Returns a floating-point random value between `0.0` and `1.0`.

#### `System.GetEnv(name)`

Returns the value of the specified environment variable.

#### `System.Args()`

Returns the command-line arguments passed to the program.

---

### File System

#### `System.ReadFile(path)`

Reads a file and returns its contents as a string.

#### `System.WriteFile(path, content)`

Writes content to a file and returns a boolean indicating success.

#### `System.Exists(path)`

Returns a boolean indicating whether the specified path exists.

#### `System.FileSystem()`

Returns an object describing the current filesystem context.

Example structure:

```jdx
{
  cwd: "...",
  home: "...",
  temp: "...",
  root: "/"
}
```

---

### System Information

#### `System.ShowSystemInfo()`

Prints runtime, operating system, CPU, memory, process, filesystem, environment, and time information to standard output.

---

### Exit Control

#### `System.Exit(code)`

Terminates the program with the given exit code.

---

### Buffer API

#### `System.Buffer()`

Creates and returns a mutable buffer object.

#### Methods

- `length()`
- `clear()`
- `write(value)`
- `appendByte(value)`
- `toString()`
- `toBytes()`
- `get(index)`
- `set(index, value)`
- `slice(start?, end?)`

#### Behavior

- `write(value)` accepts strings, arrays of numbers, or buffer-like objects
- `appendByte(value)` appends a byte in the range `0–255`
- `get(index)` supports negative indexing
- `set(index, value)` supports negative indexing and may extend the buffer when required
- `slice(start, end)` supports negative indices and returns a new buffer

#### Example

```jdx
let buf = System.Buffer();
buf.appendByte(65);
buf.appendByte(66);
buf.appendByte(67);

Print(buf.toString());  // "ABC"
Print(buf.get(1));      // 66

buf.set(1, 90);
Print(buf.toString());  // "AZC"
```

---

### Regular Expression API

#### `System.Regex(pattern[, flags])`

Creates and returns a regular expression object.

#### Supported Flags

- `i` → case-insensitive
- `n` → nosubs
- `o` → optimize
- `c` → collate

#### Methods

- `test(input)` → boolean
- `match(input)` → boolean
- `search(input)` → object
- `replace(input, replacement)` → string
- `split(input)` → array

#### Properties

- `kind`
- `pattern`
- `flags`

#### `search(input)` Result Shape

The `search` method returns an object of the following form:

```jdx
{
  matched: true/false,
  input: "...",
  value: "...",   // present only on match
  index: 0,       // present only on match
  length: 0,      // present only on match
  groups: [...]   // present only on match
}
```

---

### Exit Signals

#### `System.ExitSignals()`

Returns an object containing platform-dependent signal constants, such as:

- `SIGINT`
- `SIGTERM`
- `SIGKILL`
- `SIGSEGV`

Additional signals may be available depending on the operating system and runtime build.

---

### Networking and Server Utilities

The runtime includes a `System.Server` namespace.

#### Available APIs

- `Socket()`
- `Resolver(host)`
- `Connect(host, port)`
- `Listen(port[, backlog])`
- `JsonParse(text)`
- `JsonStringify(value)`
- `JsonStringfy(value)` (legacy alias)
- `ResponseHeader(...)`

---

### Socket Object

Socket objects expose the following methods:

- `connect(host, port)`
- `send(data)`
- `recv(size?)`
- `close()`
- `setTimeout(ms)`
- `info()`

#### `info()` Return Shape

```jdx
{
  fd: ...,
  connected: ...,
  listening: ...
}
```

---

### Listener Object

Listener objects expose the following methods:

- `accept()`
- `close()`
- `info()`

#### `info()` Return Shape

```jdx
{
  fd: ...,
  listening: ...
}
```

---

### Host Resolution and Connections

#### `System.Server.Resolver(host)`

Returns an array of resolved IP addresses for the given host.

#### `System.Server.Connect(host, port)`

Creates a socket and connects immediately.

#### `System.Server.Listen(port[, backlog])`

Creates a listening socket.

---

### JSON Utilities

#### `System.Server.JsonParse(text)`

Parses a JSON string into a runtime value.

#### `System.Server.JsonStringify(value)`

Serializes a runtime value into a JSON string.

---

### HTTP Response Header Utility

#### `System.Server.ResponseHeader(statusCode[, contentType[, body[, extraHeaders]]])`

Generates an HTTP header string.

#### Example

```jdx
let header = System.Server.ResponseHeader(200, "text/plain; charset=utf-8", "Hello");
```

---

## Built-in Modules

The runtime provides several standard modules under the `jdx:` namespace.

---

### `jdx:math`

Exports:

- `add(a, b)`
- `double(n)`
- default export: `multiply(a, b)`

#### Example

```jdx
import multiply, { add, double } from "jdx:math";
```

---

### `jdx:time`

Exports:

- `now()` → formatted current time
- `now_ms()` → current timestamp in milliseconds

---

### `jdx:fs`

Exports:

- `readFile(path)`
- `writeFile(path, content)`
- `exists(path)`
- `cwd()`
- `home()`
- `temp()`
- `root()`

---

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
- `ExitSignal(signal)`
- `Buffer()`
- `Regex(pattern)`
- `RegexTest(pattern, input)`
- `RegexMatch(pattern, input)`
- `RegexSearch(pattern, input)`
- `RegexReplace(pattern, input, replacement)`
- `RegexSplit(pattern, input)`

---

### `jdx:regex`

Exports:

- `Compile(pattern)`
- `Test(pattern, input)`
- `Match(pattern, input)`
- `Search(pattern, input)`
- `Replace(pattern, input, replacement)`
- `Split(pattern, input)`

---

## Examples

### Variables and Arithmetic

```jdx
let a = 10;
let b = 20;
let c = a + b * 2;
Print(c);
```

### Functions

```jdx
function greet(name) {
  return "Hello, " + name;
}

Print(greet("Sobri"));
```

### Conditional Logic

```jdx
let n = 7;

if (n > 10) {
  Print("large");
} elif (n > 5) {
  Print("medium");
} else {
  Print("small");
}
```

### Looping

```jdx
for (let i = 0; i < 5; i = i + 1) {
  Print(i);
}
```

### Importing a Module

```jdx
import { add } from "jdx:math";
Print(add(2, 3));
```

### Default Export

```jdx
export default function multiply(a, b) {
  return a * b;
}
```

### Buffer Usage

```jdx
let buf = System.Buffer();
buf.appendByte(65);
buf.appendByte(66);
buf.appendByte(67);

Print(buf.toString());
```

### Regex Usage

```jdx
let re = System.Regex("hello", "i");
Print(re.test("HeLLo"));
```

---

## Implementation Notes

### Semicolon Requirements

The following constructs must terminate with a semicolon:

- `let` declarations
- `const` declarations
- `return`
- `break`
- `continue`
- expression statements
- import declarations
- `export default` expressions
- named export declarations

### Unsupported Grammar Constructs

The parser and runtime do **not** currently provide native syntax for the following:

- logical `&&` / `||`
- ternary conditional `?:`
- array indexing syntax such as `arr[0]`
- object literal syntax such as `{ a: 1 }`
- class declarations
- `switch`
- `try/catch`
- increment/decrement operators `++` / `--`

### Array and Object Literals

Although the runtime supports array and object value types, the grammar does not expose direct array literal or object literal syntax in the parser. Such values are typically introduced through:

- native APIs
- module imports
- runtime factories such as `System.Buffer()`
- server / filesystem / JSON utilities

### Property Access Flexibility

Member access after `.` is permissive enough to support common native-style property names, including some reserved words used as property identifiers in native objects.

---

## Summary

JDX provides a compact scripting language with a small but practical grammar, a native runtime object model, module loading, and utility APIs for string processing, I/O, files, buffers, regex, time, and networking.

It is suitable for:

- automation
- embedded scripting
- rapid utility development
- runtime-controlled workflows
- module-based tooling
