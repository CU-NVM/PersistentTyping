# ycsb, single-threaded, persistent with PMDK (libpmemobj++)
Same benchmark as the DRAM version of `../ycsb` (no `persistent<>` annotations), with `HashNode`/`HashTable` hand-ported to libpmemobj++
(`p<>`, `persistent_ptr<>`, `make_persistent`, `transaction::run`) and the tables hanging off the pool root.
Lookups and `get` run outside transactions; `insert` and `update` are one transaction each, and `update`
snapshots the whole payload once.

compile:
```make```

run (the pool is reused across runs; delete it for a fresh run):
```shell
PERSISTENT_POOL_PATH=/mnt/pmem-emu/ycsb_pmdk_pool ./bin/ycsb -w A -u 20 -i 10 -k 1000000 -b 100000 -a 10 -p 64 -m uniform -H djb2 -W 2
```
On a regular (non-DAX) file add `PMEM_IS_PMEM_FORCE=1`, or every flush becomes an msync.
