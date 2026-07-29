# Depot Language Reference

## Datatypes

- signed 64 bit integer value

```depot
1337 // STACK: 1337:int64
// <base>#<num>, where base is in range [2, 36]
2#1001 // STACK: 1337:int64 9:int64
// bases bigger than 10 use latin alphabet letters (any case, even mixed)
16#CaFe // STACK: 1337:int64 9:int64 51966:int64
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

- `if` -- begin if block

```depot
<cond> if
    <body>
else <cond2> elif
    <body2>
// ...
eles <condN> elif
    <bodyN>
else
    <bodyN+1>
then
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

- `mod` -- remained of `/`

Signature `x y -- x%y`

- `=` -- equality comparison

Signature `x y -- x==y`

- `!=` -- not-equal comparison

Signature `x y -- x!=y`

- `<` -- less-than comparison

Signature `x y -- x<y`

- `<=` -- less-than-or-equal comparison

Signature `x y -- x<=y`

- `>` -- greater-than comparison

Signature `x y -- x>y`

- `>=` -- greater-than-or-equal comparison

Signature `x y -- x>=y`

- `@<size>` -- dereference a pointer to value of size `<size>`

Available sizes (**NOTE:** architecture dependant):
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
