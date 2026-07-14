# Depot Language Reference

## Datatypes

- signed 64 bit integer value

```depot
1337 // STACK: int64
```

- string

```depot
"Hello, World!" // STACK: int64 ptr
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

Signature `x y -- x*y`
