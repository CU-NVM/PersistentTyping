# ycsb, single-threaded, persistent with persist-clang-tool
Single-threaded ycsb: one thread, `--tables` hash tables, no locks, no NUMA. The only persistence
annotation in the user code is that the tables are `persistent<HashTable>` (see
`src/ycsb_benchmark.cpp`). `../ycsb_pmdk` is the hand-written PMDK version it is compared against.

persistify (writes `../Output/ycsb/`, see `../persistify.py`):
```shell
../persistify.py ycsb
```

compile + run the persistified copy:
```shell
../run.py ycsb -- -w A -u 20 -i 10 -k 1000000 -b 100000 -a 10 -p 64 -m uniform -H djb2 -W 2
```
The pool is reused across runs (`--fresh` deletes it). Build with clang++ (the Makefile does):
under g++ the pool handle is reset after it is opened. On a regular (non-DAX) file add
`PMEM_IS_PMEM_FORCE=1`, or every flush becomes an msync.
