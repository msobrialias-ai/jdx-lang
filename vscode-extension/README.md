# JDX Language Tools

![Logo](logo.svg)

VS Code extension for the **JDX** language.

## Features

- Syntax highlighting
- Editor snippets
- Autocomplete for keywords, runtime namespaces, and local symbols
- Hover docs for keywords and runtime objects
- Built-in diagnostics that Error Lens can render
- Basic linting for common syntax mistakes

## Language summary

JDX supports:

- `let` and `const`
- `fname` function declarations
- `class` declarations
- `if / elif / else`
- `while` and `for`
- `try / catch`
- `import` and `export`
- literals: `true`, `false`, `null`, strings, numbers

## Example

```jdx
import { readFile } from "jdx:fs";

fname main() {
    const text = readFile("hello.jdx");
    System.Output(text);
}
```

## Runtime namespaces

- `System`
- `System.FileSystem`
- `System.Info`
- `System.JGex`
- `System.Server`
- `System.SafeExec`
- `Develoment`
- `Develoment.Stacktrace`
- `Develoment.Test`

## Diagnostics

The built-in linter checks for:

- unmatched braces, brackets, and parentheses
- unterminated strings and block comments
- missing semicolons on common statement forms
- `const` declarations without initializers
- malformed `fname`, `class`, `import`, and `catch` forms

## Installation

Package the extension as a `.vsix`, then install it in VS Code through:
`Extensions` → `...` → `Install from VSIX...`

## Notes

The diagnostics are intentionally lightweight. They are designed to catch common mistakes early and to work well alongside Error Lens.
