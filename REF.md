# Depot Language Reference

## Datatypes

- signed 64 bit integer value (`int64`)

```depot
1337 // STACK: 1337:int64
// <base>#<num>, where base is in range [2, 36]
2#1001 // STACK: 1337:int64 9:int64
// bases bigger than 10 use latin alphabet letters (any case, even mixed)
16#CaFe // STACK: 1337:int64 9:int64 51966:int64
```

- string (`int64 ptr`)

```depot
"Hello, World!" // STACK: length:int64 mem:ptr
```

- logical/boolean (`bool`)

```depot
true // STACK: true:bool
false // STACK: true:bool false:bool
```

### Type Signatures

Datatypes can be used inside of type signatures:

```depot
[input-types] [-- [return-types]]
```

If no `[return-types]` are present, then `--` can be ommited

## Built-in Words

- `proc` -- define a procedure

```depot
proc <name> [type-sig] {
    <body>
}
```

- `dispatch` -- define a dispatchable procedure

```depot
dispatch <dispatch-name> proc <proc-name> [type-sig] {
    <body>
}
```

When `<dispatch-name>` is encountered in the source code, current stack state is
matched against all procedure with this `<dispatch-name>` and if there is
exactly 1 (one) compatible procedure it will called automatically. If the call
is ambiguous (i.e. there is more than one procedure with compatible signature)
the `<proc-name>` can be used, instead of `<dispatch-name>`.

A better name for this functionality would be "explicit procedure overloading"
(by @LensPlaysGames on GitHub).

- `link` -- link with library

Passed as `-l<lib>` to linker

```depot
link "<lib>"
```

- `extern` -- define an external procedure

```depot
extern "[external-symbol-name]" proc <name> [type-sig] ;
```

If `[external-symbol-name]` is not present, then `<name>` is used instead.

- `include` -- include another Depot source

```depot
include "<file_path>"
```

- `memory` -- static global memory allocation definition

```depot
memory <name> <size:int64>
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

- `rot` -- exchange the top three elements

Signature: `x y z -- z y x`

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

- `>int64` -- convert top element to an `int64`

Signature `x:any_type -- x:int64`

- `>bool` -- convert top element to a `bool`

Signature `x:any_type -- x:bool`

- `>ptr` -- convert top element to a `ptr`

Signature `x:any_type -- x:ptr`
