# C++ Knowledge Base: Features Used in `persistent<T>`

A running reference for C++ features that come up in this project. Same style as [OS_knowledge.md](OS_knowledge.md) — read top to bottom once, then use as a lookup.

Scope: features used in `pracitce/ex1_persist.cpp`, `pracitce/ex2_stack.cpp`, `persistentLib/pmem_allocator.hpp`, `persistentLib/persistenttype.hpp`, and (looking ahead) the specializations coming in Round 3.

---

## Table of Contents

1. [Class Templates](#1-class-templates)
2. [Function Templates](#2-function-templates)
3. [Variadic Templates and Parameter Packs](#3-variadic-templates-and-parameter-packs)
4. [Perfect Forwarding (`std::forward`)](#4-perfect-forwarding-stdforward)
5. [Template Specialization](#5-template-specialization)
6. [Type Aliases (`using` vs `typedef`)](#6-type-aliases-using-vs-typedef)
7. [Nested Type Members](#7-nested-type-members)
8. [Special Member Functions](#8-special-member-functions)
9. [Lambdas and Captures](#9-lambdas-and-captures)
10. [Placement New](#10-placement-new)
11. [Operator Overloading](#11-operator-overloading)
12. [`inline` Variables and Functions](#12-inline-variables-and-functions)
13. [`noexcept`](#13-noexcept)
14. [GCC/Clang Attributes](#14-gccclang-attributes)
15. [Header Guards](#15-header-guards)

---

## 1. Class Templates

A class template is a class parameterized over one or more types or values. The compiler generates a concrete class for each unique combination of template arguments it sees.

```cpp
template <typename T>
class PersistentAllocator { ... };
```

Each use — `PersistentAllocator<int>`, `PersistentAllocator<double>`, `PersistentAllocator<Node>` — is a separate, fully-formed class type, with its own copies of every member. They share no code at runtime; the compiler instantiates each one independently.

**Why this matters:** the STL uses templates heavily because containers need to work for any element type without losing type safety. `std::vector<int>` and `std::vector<double>` are different types — you cannot assign one to the other — but they share the same source code.

**Instantiation is lazy.** The compiler only generates a function body when something actually calls it. If you have a `template<typename T> void f(T x) { x.foo(); }` and nothing in your program calls `f(SomeType)`, the body is never instantiated. This is why templates can refer to members of `T` that don't exist for all types — if you don't trigger the instantiation, it doesn't matter.

---

## 2. Function Templates

Same idea, but for functions. The classic example from our code:

```cpp
template <typename U, typename... Args>
void construct(U *p, Args&&... args) {
    ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
}
```

This is one source-level function definition but the compiler generates a new instantiation for every distinct `(U, Args...)` combination called with.

**Template argument deduction:** when you call `construct(my_int_ptr, 42)`, the compiler deduces `U = int` and `Args = {int}` from the argument types. You almost never have to spell out the template arguments at the call site for function templates — they're inferred.

---

## 3. Variadic Templates and Parameter Packs

`typename... Args` declares a **template parameter pack** — zero or more type parameters. `Args&&... args` is a function parameter pack — zero or more parameters of those types.

```cpp
template <typename U, typename... Args>
void construct(U *p, Args&&... args);
```

This `construct` accepts:
- `construct(p)` → `Args` is empty
- `construct(p, 42)` → `Args = {int}`
- `construct(p, "hello", 3.14, std::move(x))` → `Args = {const char*, double, X}`

**Pack expansion** uses `...` after an expression. `std::forward<Args>(args)...` is short for `std::forward<Arg0>(arg0), std::forward<Arg1>(arg1), ...`. The pattern before the `...` is repeated for each element, and the `...` separates them with commas.

Variadic templates are why `std::vector::emplace_back(args...)` can forward any number of constructor arguments to construct an element in place.

---

## 4. Perfect Forwarding (`std::forward`)

The combination of `Args&&...` and `std::forward<Args>(args)...` is **perfect forwarding** — the most important idiom in modern C++ template code.

### The problem it solves

When you write a "forwarding" function like `construct`, you want it to behave *transparently* — if the caller passed an rvalue (`std::move(x)`), the destination constructor should see an rvalue and move. If the caller passed an lvalue (`x`), the destination should see an lvalue and copy.

Without perfect forwarding, by the time `args` is sitting inside `construct`, it's a named variable — which makes it an **lvalue** regardless of what was passed in. So if you just wrote `U(args...)`, every argument would be treated as an lvalue, and you'd silently lose all move semantics.

### How it works

- `Args&&` in a *deduced* template context is a **forwarding reference** (also called "universal reference"). It binds to both lvalues and rvalues, and the deduced type `Args` records which one was passed:
  - Call with lvalue `int x; f(x)` → `Args = int&` (deduction adds `&` for lvalues)
  - Call with rvalue `f(42)` → `Args = int` (no `&` for rvalues)
- `std::forward<Args>(args)` looks at `Args` and produces either an lvalue reference or an rvalue reference accordingly. It's a `static_cast` in disguise — exactly the cast needed to restore the original value category.

This is why you see the exact triple — `template <typename... Args>` + `Args&&... args` + `std::forward<Args>(args)...` — together. They form one idiom. Break any one of them and forwarding silently degrades to copy.

---

## 5. Template Specialization

Sometimes you want a template to behave differently for specific types. C++ lets you provide an alternative definition for particular template arguments.

### Full specialization

```cpp
template <typename T> struct IsInt { static constexpr bool value = false; };
template <> struct IsInt<int> { static constexpr bool value = true; };
```

`IsInt<double>::value` is `false`, `IsInt<int>::value` is `true`.

### Partial specialization

You can specialize on a *pattern* rather than a single type:

```cpp
template <typename T> struct Foo { ... };           // primary
template <typename T> struct Foo<T*> { ... };       // partial: for any pointer type
template <typename T> struct Foo<T const> { ... };  // partial: for any const T
```

The compiler picks the most specific match.

### SFINAE — "Substitution Failure Is Not An Error"

If a template argument substitution would produce an ill-formed type, that candidate is silently removed from overload resolution rather than producing a compile error. This is the mechanism behind `std::enable_if`:

```cpp
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
void only_for_integers(T x);
```

If you call `only_for_integers(3.14)`, `std::enable_if_t<false>` is ill-formed → the overload is removed → you get "no matching function" instead of a confusing error inside the body.

**You'll see this in Round 3.** The numa library uses it to dispatch:

```cpp
template<typename T, int NodeID, template <typename, int> class Alloc>
class numa<T, NodeID, Alloc,
           typename std::enable_if<std::is_fundamental<T>::value
                                || std::is_pointer<T>::value>::type> { ... };
```

→ this specialization is only viable when `T` is a fundamental type or pointer. Different specialization for class types. Same template name, two definitions, compiler picks based on `T`.

---

## 6. Type Aliases (`using` vs `typedef`)

```cpp
using value_type = T;        // modern, preferred
typedef T value_type;        // old C-style, equivalent
```

Both create an alias for an existing type. `using` is preferred in modern C++ because:
- It reads left-to-right: "value_type *is* T" matches normal variable-declaration order
- It supports templated aliases:
  ```cpp
  template <typename T>
  using Vec = std::vector<T, MyAlloc<T>>;        // works with `using`
  ```
- `typedef` cannot template-alias like this.

In our `PersistentAllocator`, every required STL type is exposed via `using`:

```cpp
using value_type = T;
using pointer = T*;
using size_type = std::size_t;
```

These aren't optional ornaments — STL containers query them by name (`Alloc::value_type`, `Alloc::pointer`, etc.) at compile time.

---

## 7. Nested Type Members

You can declare a type *inside* a class, and refer to it as `Outer::Inner`:

```cpp
class PersistentAllocator {
public:
    template <typename U>
    struct rebind {
        using other = PersistentAllocator<U>;
    };
};

// Used as:
using NodeAlloc = PersistentAllocator<int>::rebind<Node>::other;
```

The nested type `rebind` is itself a template. To access the inner alias, you walk down: `OuterClass::InnerTemplate<U>::aliasName`.

**`typename` keyword inside templates.** When you write `T::something` and `T` is itself a template parameter, the compiler doesn't know if `something` is a type or a value. You disambiguate with `typename`:

```cpp
template <typename Alloc>
void f() {
    typename Alloc::value_type x;        // "Alloc::value_type is a TYPE, not a value"
}
```

Without `typename`, the compiler assumes `Alloc::value_type` is a value, and `Alloc::value_type x;` parses as a multiplication. This shows up constantly in template metaprogramming.

---

## 8. Special Member Functions

Every class has six special member functions the compiler can auto-generate:

| Function | When generated by default |
|---|---|
| Default constructor `T()` | If you declare *no* constructors |
| Copy constructor `T(const T&)` | If you don't define one and don't `=delete` it |
| Copy assignment `T& operator=(const T&)` | Same |
| Move constructor `T(T&&)` | If you don't define copy/destructor/etc. (rule of zero/five) |
| Move assignment `T& operator=(T&&)` | Same |
| Destructor `~T()` | Always, unless you `=delete` it |

In `PersistentAllocator`, we manually wrote:

```cpp
PersistentAllocator() noexcept {}                                   // default
PersistentAllocator(const PersistentAllocator&) noexcept {}         // copy
template <typename U>
PersistentAllocator(const PersistentAllocator<U>&) noexcept {}      // converting
```

The third is a **converting constructor** — it's a template, not a special member. It allows implicit conversion between `PersistentAllocator<int>` and `PersistentAllocator<Node>` (different types — they're different template instantiations). This is what enables STL's `rebind`-and-then-construct dance.

**Rule of three / five / zero:** if you write any of {destructor, copy ctor, copy assignment, move ctor, move assignment}, you usually need to write or `=delete` all of them. If you don't write any, the compiler generates them all (rule of zero — preferred).

---

## 9. Lambdas and Captures

A lambda is an anonymous function object — syntactically lightweight, semantically a class with `operator()`. We used them in `transaction::run`:

```cpp
transaction::run(pop, [&] {
    r->counter = r->counter + 1;
});
```

The square brackets `[...]` are the **capture list** — they declare which surrounding variables the lambda body can use, and how:

| Capture | Meaning |
|---|---|
| `[]` | No captures. Body cannot reference outer variables. |
| `[x]` | Capture `x` by value (a copy is stored in the lambda). |
| `[&x]` | Capture `x` by reference. The lambda holds a reference to the outer `x`. |
| `[=]` | Capture *all used* outer variables by value. |
| `[&]` | Capture *all used* outer variables by reference. |
| `[this]` | Capture the enclosing object's `this` pointer. |
| `[x, &y]` | Mix: `x` by value, `y` by reference. |

**`[&]` is what we used inside `transaction::run`** — the lambda needs to read and write the persistent variables in the surrounding scope. It captures `r` and `pop` (and anything else referenced) by reference.

**Trade-offs:**
- `[&]` is convenient but risky if the lambda outlives the captured variables (dangling reference).
- `[=]` is safe in lifetime terms but copies all captured state — expensive for big objects.
- For long-lived lambdas (callbacks, async tasks), prefer explicit named captures so it's clear what state the lambda owns.

---

## 10. Placement New

`new T(args)` does two things: allocates raw memory and constructs a `T` in it. **Placement new** separates these — you provide pre-allocated memory and just construct in it:

```cpp
void *raw = pmem_alloc(sizeof(T), alignof(T));  // step 1: get memory
T *obj = ::new (raw) T(args...);                // step 2: construct in place
```

Syntax breakdown:
- `::new` — explicitly use the global `operator new`, bypassing any class-overloaded one. Important: if `T::operator new` is defined, `new T(...)` would call it, allocating memory again. `::new (raw) T(...)` does not allocate; it uses `raw`.
- `(raw)` — the **placement argument**. Tells `new` to use this address.
- `T(args...)` — the constructor call.

To destroy a placement-new'd object, call the destructor explicitly:

```cpp
obj->~T();           // run destructor
pmem_free(raw);      // release memory
```

This split is exactly how STL allocators work: `allocate()` returns raw memory, `construct()` placement-news into it, `destroy()` calls the destructor, `deallocate()` releases the memory. A `vector` with capacity 100 and size 3 has *allocated* 100 slots and *constructed* only 3 of them.

---

## 11. Operator Overloading

You can define what built-in operators mean for your types:

```cpp
template <typename T1, typename T2>
bool operator==(const PersistentAllocator<T1>&, const PersistentAllocator<T2>&) noexcept {
    return true;
}
```

Operators can be member functions or **free functions**. Comparison operators are usually free functions because they're symmetric — `a == b` and `b == a` should behave the same. Member-function operators are asymmetric: the left operand is `*this`.

Coming in Round 3 you'll write member operators:

```cpp
inline operator T&() { return contents; }        // implicit conversion to T&
inline T operator->() { ... }                    // arrow operator
numa& operator=(const T& data) { ... }           // assignment from a T
```

- `operator T&()` is a **conversion operator** — lets the numa object be used wherever a `T&` is expected. Implicit, dangerous if abused; here it's intentional so `numa<int, 0>` can act like an `int`.
- `operator->()` is the arrow operator overload — used when `T` is a pointer type so `n->field` works.
- `operator=` is overloaded assignment — lets you write `n = 5` to set the underlying value.

---

## 12. `inline` Variables and Functions

`inline` started as "please inline this function call" (a hint, not a guarantee). In modern C++, that meaning is mostly gone — compilers inline based on their own heuristics. The *real* meaning today is about the One Definition Rule:

> If you define a function or variable in a header, and the header is included in multiple translation units, the linker will see multiple definitions and fail. **`inline` tells the linker: "expect multiple definitions, they're all the same, merge them."**

This is why our `pmem_allocator.hpp` has:

```cpp
inline PMEMobjpool *global_pool = nullptr;     // C++17 inline variable
inline void pmem_alloc_init() { ... }
```

Without `inline`, including `pmem_allocator.hpp` from two `.cpp` files would produce two `global_pool` definitions and two `pmem_alloc_init` definitions, and the linker would refuse.

**Inline variables (C++17)** are new — before C++17, you couldn't define a non-const variable in a header without using `static` (which makes a *separate* copy per TU, usually wrong) or `extern` + a separate `.cpp` definition.

Template functions and class member functions defined inside the class body are *implicitly* inline, so you don't see `inline` on member functions of templates.

---

## 13. `noexcept`

A function annotation declaring "this function does not throw exceptions":

```cpp
PersistentAllocator() noexcept {}
void deallocate(pointer p, size_type n) noexcept;
```

Two reasons to use it:

1. **Documentation / contract.** Tells callers the function won't throw, so they don't need to plan for exception cleanup at this call site.
2. **Optimization.** Some STL operations have a fast path when their members are `noexcept`. The classic example: `std::vector` only uses move (instead of copy) during reallocation if the element's move constructor is `noexcept` — because if a move could throw mid-reallocation, the vector would be left in an inconsistent state. So a non-`noexcept` move constructor silently disables a major optimization.

**If a `noexcept` function does throw, `std::terminate` is called.** It's a hard promise, not advisory.

For allocator special members, `noexcept` is part of the conventional contract — STL containers expect allocators not to throw on copy.

---

## 14. GCC/Clang Attributes

`__attribute__((...))` is a compiler extension for adding metadata to functions, variables, or types. Not standard C++, but widely supported by GCC and Clang.

### Used in this project

```cpp
__attribute__((constructor))
inline void pmem_alloc_init() { ... }

__attribute__((destructor))
inline void pmem_alloc_fini() { ... }
```

- `constructor`: the function is called automatically **before `main`** when the program loads.
- `destructor`: called automatically **after `main` returns** (or after `exit()`).

This is how our allocator opens and closes the pmem pool without the user having to remember to do it. Note that ordering between multiple constructors from different translation units is unspecified — don't rely on it.

### Other useful ones (in numa code)

```cpp
inline T load() __attribute__((always_inline)) { return contents; }
```

- `always_inline`: hint stronger than `inline` — refuse to compile rather than fall back to a call. Used for performance-critical primitives.

The C++23 standard syntax `[[gnu::constructor]]` exists but is less portable in practice. Stick with `__attribute__((...))` for now.

---

## 15. Header Guards

A header included from multiple `.cpp` files (or from another header twice via different paths) would otherwise be processed multiple times, redeclaring everything and triggering "redefinition" errors.

Two equivalent mechanisms, usually combined:

```cpp
#pragma once          // non-standard but widely supported, simple

#ifndef _PMEM_ALLOCATOR_HPP_
#define _PMEM_ALLOCATOR_HPP_

// ... header content ...

#endif
```

- `#pragma once` tells the compiler "include this file at most once per translation unit." Faster than the macro guard because the compiler can short-circuit on the second include without reparsing.
- The `#ifndef/#define/#endif` guard is the traditional standard-portable form. It works by defining a unique macro on first inclusion, and skipping content on subsequent inclusions.

Belt-and-suspenders style (both) is common and harmless. The macro name should be unique to the header — using something like the path with all the slashes replaced by underscores.
