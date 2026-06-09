# Linting

The built-in linter is intentionally lightweight. It flags:

- unmatched `(` `)` `{` `}` `[` `]`
- unterminated strings
- unterminated block comments
- missing semicolons on statement-like lines
- `const` declarations without an initializer
- malformed `fname`, `class`, `import`, and `catch` syntax

This is not a full type checker or semantic analyzer. It is a fast first-pass diagnostics layer for the editor.
