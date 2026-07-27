# Depot Language Reference

## Datatypes

- signed 64 bit integer value

```depot
1337 // STACK: 1337:int64
```

- string

```depot
"Hello, World!" // STACK: length:int64 mem:ptr
```

## Built-in Words

- `proc` -- define a procedure

```depot
proc <name> {
    <body>
}
```

- `link` -- link with library

Passed as `-l<lib>` to linker

```depot
link "<lib>"
```

- `extern` -- define an external procedure

```depot
extern proc <symbol-name> <arity> ;
```

- `while` -- while loop block

> [!NOTE]
> `<cond>` can access outer stack

```depot
while <cond> {
    <body>
}
```

- `drop` -- remove top element

Signature: `x --`

- `dup` -- duplicate element on the top

Signature: `x -- x x`

- `swap` -- exchange the top two elements

Signature: `x y -- y x`

- `over` -- place a copy of an element below the top on the top

Signature: `x y - x y x`

- `+` -- sum up the top two elements

Signature `x y -- x+y`

- `-` -- subtract top element from element below the top

Signature `x y -- x-y`

- `*` -- multiply the top two elements

Signature `x y -- x*y`

- `/` -- divide element below the top by the top element

Signature `x y -- x/y`

- `@<size>` -- dereference a pointer to value of size `<size>`

Available sizes (architecture dependant):
- 64
- 32
- 16
- 8

Signature `x-addr -- x`

- `!<size>` -- store a value of size `<size>` to memory

Available sizes (**NOTE:** architecture dependant):
- 64
- 32
- 16
- 8

Signature `x x-addr --`
