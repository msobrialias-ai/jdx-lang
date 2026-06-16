# JDX Syntax

## Declarations

```jdx
let name = value;
const count = 1;
fname add(a, b) {
    return a + b;
}
class Point {
    fname init(x, y) {
        this.x = x;
        this.y = y;
    }
}
```

## Control flow

```jdx
if (condition) {
    ...
} elif (otherCondition) {
    ...
} else {
    ...
}

while (condition) {
    ...
}

for (let i = 0; i < 10; i = i + 1) {
    ...
}
```

## Modules

```jdx
import "jdx:fs";
import { readFile, writeFile as saveFile } from "jdx:fs";
import name, { item } from "./module.jdx";

export { name as publicName };
export default fname main() {
    ...
}
```

## Expressions

JDX supports:

- function calls: `fn(a, b)` and `await task`
- property access: `object.name`
- assignment: `name = value`
- arithmetic: `+ - * / %`
- comparison: `== != < <= > >=`
- logical operators: `&& || !`
