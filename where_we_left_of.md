# Where we left off (2026-11-09)

Branch `main`, last commit `ed4e5d98` (closure to-do #4). **Everything below is
uncommitted** in the working tree (about 70 files). All suites pass:

```bash
(cd persist-clang-tool/build && make -j8 && ./bin/translate-type-test)   # 86/86
(cd tests/lib && ./run_tests.py)                                         # 17/17
(cd tests/checkPersistentTypes && ./run_tests.py)                        # 12/12
(cd tests/rootSetup && ./run_tests.py --e2e)                             # 50/50
```

The thesis repo (`~/thesis`) has unpushed `design.md` commits; push them when
everything is implemented, as planned.

## Done since the last commit

| Closure to-do | What changed | Main files |
|---|---|---|
| #1, #2 | The library never opens a transaction itself (except `pmem_get_or_create`). Every write, `new` and `delete` in the pool calls `pmem_require_tx` and throws outside one; `delete` ends in `std::terminate`, since `operator delete` is `noexcept`. Before, PMDK *aborted* the process on a write outside a transaction, a pmem pointer opened its own transaction, and `new`/`delete` ran non-transactionally. | `persistentLib/pmem_allocator.hpp`, `persistenttype.hpp`, `tests/lib` (`outside_tx`) |
| #6 | Only a root's **definition** creates its object, and only while its slot is null. Each global's creation is in its own `__pers_init_<root>()` at the end of its file; `main()` calls them first, each after the roots its initializer uses (dependency order), otherwise in file order and then declaration order. Uses of roots inside a moved initializer are rewritten (your idea). A static local's creation becomes `static bool __pers_init_n = (pmem_get_or_create<...>(slot, args), true);`, which C++ runs once, when first reached; its uses are the slot, like a global's. **`r = new ...` anywhere else is an ordinary assignment**, no longer get-or-create. By-value roots `persistent<int> runs = 100;` / `persistent<List> list(16);` (shape `Value`, used as `(*slot)`). | `transformer/rootSetup.{h,cc}` |
| bug fix | recurse-persistent corrupted a header shared by two `.cpp` files that defines a class reached from a root (it was specialised twice, because one `ClangTool` caches the old file text). It now runs one `ClangTool` per file. This would have hit YCSB. | `main.cc` (`runPerFile`) |
| bug fix | root-setup's "two roots in one declaration" check compared offsets without the file, so a `.cpp` declaration at the same offset as one in a header was wrongly rejected. | `rootSetup.cc` (`globalBegins`) |

Old-design tests that were deleted (recoverable from git): `tests/pmem_allocator_test.cpp`,
`tests/persistent_allocator_test.cpp`, and the rootSetup case
`err_array_new_in_expression`.

Details for each item are in `HANDOFF.md` (dated entries at the top of "What's
been done so far"), `Docs/clang-tool.md` ("Roots") and `design.md` §2.2 and §4.2.

## Closure to-do #7: done (2026-11-09)

Root keys are qualified names (`a::x`, `(anonymous namespace)::x`,
`f(int)::cache`), fields are those keys as identifiers (`a__x`, `f_int___cache`),
and two file-private roots with the same name in different files, or a static
local of a lambda, are errors. This fixes `a::x`/`b::x` and same-named static
locals silently sharing one slot. Details in `HANDOFF.md`.

## After that (closure document §14)

- **#19** Respell the `persistify.py` benchmarks (counter, stack, hanoi, kvstr,
  kvnode, ycsb) to the new root spelling. They are the only end-to-end tests of
  real programs, and today the tool finds no roots in them.
- **#5** Leak warning with fix-it for `new persistent<...>` stored only in a
  static DRAM variable.
- **#9, #10, #11, #21** Small diagnostics: persistent static data members,
  `thread_local` (decision still open), parameters/returns/temporaries of
  persistent type, `virtual`.
- **#13** The free-function parse fix (unblocks #14 to #16 and #18). Until then,
  a free function cannot walk pmem data before the transform: `p->next` is a
  `Node*`.
- **#17** Layout hash (#7 is done, so this is unblocked).

## Known gaps to remember

- A root used during C++ static initialisation (a global's initializer reading a
  root before `main`) crashes on a null slot. The planned guard in
  `__pers_get_root()` ("root used before main()") is deferred.
- A root that creates its object must be defined in a `.cpp`, not a header.
- By-value root arrays (`persistent<int> hits[4];`) are an "unsupported shape"
  error, on purpose: not expected to be needed. If they are, see the HANDOFF
  entry of 2026-11-08/09 for how to add them without a helper.
