# OS Knowledge Base

A textbook of operating-systems concepts that come up in this project. Each chapter introduces a concept with a simple, everyday example, builds intuition through explanation, then closes with a second example showing how it relates to our pmem environment and the `persistent<T>` work.

The chapters build on each other — later ones reference earlier ones — but most stand on their own.

---

## Table of Contents

1. [Memory Hierarchy](#chapter-1-memory-hierarchy)
2. [Virtual Memory and Paging](#chapter-2-virtual-memory-and-paging)
3. [The Page Cache](#chapter-3-the-page-cache)
4. [Filesystems](#chapter-4-filesystems)
5. [Mounting](#chapter-5-mounting)
6. [Memory-Mapped Files](#chapter-6-memory-mapped-files)
7. [DAX: Direct Access](#chapter-7-dax-direct-access)
8. [Persistent Memory (NVM/pmem)](#chapter-8-persistent-memory-nvmpmem)
9. [GRUB and Kernel Boot Parameters](#chapter-9-grub-and-kernel-boot-parameters)

---

## Chapter 1: Memory Hierarchy

### The idea

Computers don't have one kind of memory — they have a hierarchy. Fast, small, expensive storage sits close to the CPU; slow, large, cheap storage sits far away. The OS and the hardware together create the illusion that everything is one big address space, hiding the cost of moving data between tiers.

### Simple example

Imagine a library. You're at a desk:

- **The book in your hands** — fastest access, but you can only hold one. (Registers)
- **A small stack of books at your elbow** — quick to reach, but limited capacity. (L1 cache)
- **The shelf of books behind you** — a few seconds to grab one. (L2/L3 cache)
- **The library's main collection** — a few minutes to walk over and search. (DRAM)
- **The off-site archive** — call ahead and wait for delivery. (SSD/HDD)

You don't *want* to walk to the archive for every page you need. Instead, when you're working on a topic, you pull a small set of books to your desk, work with them quickly, and return them when you're done. This is **temporal locality** — reusing recently-accessed data. The hierarchy exploits it everywhere.

### Putting numbers on it

```
Registers       ~1 cycle        hundreds of bytes
L1 cache        ~4 cycles       32–64 KB per core
L2 cache        ~12 cycles      256 KB – 1 MB per core
L3 cache        ~40 cycles      8–64 MB shared
DRAM            ~100 cycles     GBs
NVM (Optane)    ~300 cycles     GBs – TBs, survives power loss
SSD             ~100,000 cycles TBs
HDD             ~10,000,000     TBs
```

Each tier is roughly an order of magnitude slower than the one above it, but an order of magnitude larger. The OS's job is to keep hot data near the CPU and migrate cold data outward.

### Cache lines

The unit of transfer between cache tiers is the **cache line**, not the individual byte. On x86, a cache line is 64 bytes. When you read one byte of DRAM, the CPU fetches the entire 64-byte line into L1 cache. That's why **spatial locality** matters: walking through an array sequentially is fast because most of your accesses hit cache lines already loaded by the previous access.

This also explains why writing to two unrelated variables that happen to live in the same cache line is slow under threading — both threads keep invalidating each other's copy of the line. The phenomenon is called **false sharing** and it's a common performance trap.

### Where this matters for pmem

NVM (persistent memory) sits between DRAM and SSD in the hierarchy — faster than SSD, slower than DRAM, but unlike DRAM, it survives power loss. In our project the relevant trade-off is:

- A write to DRAM completes in cache; the data is lost on power failure.
- A write to NVM completes in cache too — *but durability requires a cache flush* (`clwb` instruction) and a fence (`sfence`) to push the line all the way to the NVM medium.

So NVM is "fast like memory, durable like disk," but only if you do the work of flushing. Our Phase 2 plan for `persistent<T>::store()` is precisely to add that flush.

Cache line size matters here too: persistent writes are most efficient when they're aligned to cache-line boundaries and don't straddle two lines (would require two flushes). PMDK's allocator aligns allocations to 64 bytes for this reason.

---

## Chapter 2: Virtual Memory and Paging

### The idea

If two programs were both allowed to put data at address `0x1000`, they'd corrupt each other. **Virtual memory** solves this by giving each process its own private address space. Each program thinks it owns all of memory; the OS and CPU together translate every memory access from the program's "virtual" view to the actual physical RAM location — invisibly, on every load and store.

### Simple example

Two text editors are running at once. Both are compiled to put their main data structure at virtual address `0x1000`. Without virtual memory, they'd fight over the same byte of RAM.

With virtual memory:
- Process A's `0x1000` → physical RAM address `0x123000`
- Process B's `0x1000` → physical RAM address `0x789000`

The mapping is per-process and invisible to the program. The CPU consults a per-process **page table** on every memory access to do the translation.

### Pages and page tables

Addresses aren't translated one byte at a time — they're translated in fixed-size chunks called **pages**. The standard page size on x86 is 4 KB. Physical memory is divided into same-sized **frames**.

The page table is a tree-shaped data structure (4 levels deep on x86-64) that maps virtual page numbers to physical frame numbers. Each entry also carries permission bits (readable, writable, executable) and a "present" bit indicating whether the page is currently in physical RAM at all.

A page table walk costs ~4 memory accesses (one per level). Doing this on every load and store would be prohibitively slow, so the CPU has a small cache called the **TLB** (Translation Lookaside Buffer) holding recent translations.

### Page faults

A **page fault** happens when the CPU can't complete a translation:

- **Minor fault**: the page exists in RAM but isn't yet mapped into this process. The OS adjusts the page table and resumes. Fast.
- **Major fault**: the page is on disk (swapped out, or never loaded). The OS reads it in. Slow.
- **Segfault** (`SIGSEGV`): the address is invalid or permissions are wrong. The OS kills the process.

A function call typically involves no faults; the stack frame is already mapped. Opening a new file and reading it for the first time may trigger many minor faults as pages are pulled in.

### Copy-on-write

When you call `fork()`, the OS doesn't physically copy the parent's memory. Instead, it marks all pages in both processes as read-only and shared. The first time either process *writes* to a page, the hardware traps, the OS copies just that page, and both processes get private copies of that page. Until that write, they share physical RAM.

This is why `fork()` is fast in practice even when the process is huge: most pages are never written before `exec()` replaces the child's memory map entirely.

### Where this matters for pmem

A pmem pool file gets `mmap`'d into a process's address space — the file's contents are exposed as a virtual address range. The kernel sets up page table entries that point at the physical NVM addresses (with DAX) or at DRAM pages that mirror the file (without DAX, see Chapter 7).

In our practice exercises, we saw a concrete consequence: every time `pool::open` runs, the same pool file might get mapped at a *different virtual address*. This is why PMDK uses `PMEMoid` (a `{pool_uuid, offset}` pair) for pointers stored inside the pool — an offset is stable across reboots, while a raw virtual address isn't. The `pmemobj_direct(oid)` call converts an offset to a usable pointer for the current process run.

It's also why our `persistent<T>` wrapper's `contents` lives wherever the wrapper object lives. If the wrapper is on the stack, `contents` is at the stack's virtual address — a perfectly valid address for this run, but meaningless on the next. Only allocations through `operator new` (which routes to pmem_alloc) land in pages that are backed by the NVM file and thus survive.

---

## Chapter 3: The Page Cache

### The idea

Disk is slow. RAM is fast. If a program reads the same file twice, the OS would be wasteful to fetch from disk both times. The **page cache** is a region of DRAM the kernel uses to hold recently-touched file data, so subsequent reads can skip the disk entirely.

### Simple example

```bash
time cat /var/log/syslog > /dev/null   # first read — hits disk
time cat /var/log/syslog > /dev/null   # second read — hits cache
```

The second invocation is vastly faster. Nothing changed on disk; the kernel just remembered the file's contents in DRAM from the first read.

`free -h` shows the cache size as "buff/cache". The kernel uses every free byte of RAM for caching, automatically shrinking it when applications need more heap or stack.

### The write path

Writes are even more aggressive. A `write()` system call doesn't go to disk immediately — it goes to the cache and the page is marked **dirty**. The kernel's writeback threads flush dirty pages to disk lazily, in the background, often seconds later.

This is great for performance:
- Writes complete instantly from the program's perspective.
- Multiple small writes to the same page get coalesced into one disk I/O.
- Re-reads of just-written data hit the cache, not disk.

It's terrible for durability:
- A crash before writeback loses the data.
- A program that thinks it has written something stable may be wrong.

### `fsync` and the durability contract

To force a write to actually reach the disk medium, a program calls `fsync(fd)`. This blocks until the kernel has flushed every dirty page for that file. Databases call `fsync` at the end of every committed transaction — that's how they promise that committed data survives a crash.

The variant `fdatasync` flushes data but skips metadata updates (like file mtime). Useful when you only care about file contents.

### `O_DIRECT`

Some applications — databases especially — want to manage their own caching, and the page cache just gets in the way (double-buffering data they already have in their own buffer pool). Opening a file with the `O_DIRECT` flag bypasses the page cache entirely; every `read` and `write` is a direct DMA transfer between the user buffer and the device. The penalty: buffers and sizes must be aligned to the device block size.

### Where this matters for pmem

For traditional files on disk, the page cache is mostly helpful. For files on NVM, it's mostly wasteful. NVM is already byte-addressable and fast — copying it into a DRAM page cache page just to read or write it adds latency and doubles the memory cost.

The DAX mount option (Chapter 7) is exactly the mechanism for telling the kernel "for this filesystem, skip the page cache." Without DAX, even a pmem-backed file goes through the cache:

```
NVM → DRAM (page cache) → process buffer
```

With DAX:

```
NVM → process address space (mmap'd directly)
```

For our work, this is the difference between PMDK using real CPU cache-flush instructions for durability (`clwb + sfence`) versus falling back to `msync` (which flushes through the page cache and is much slower). When we chose `memmap=4G!4G` instead of tmpfs, this is the property we were buying — `pmem_is_pmem()` returns true on a DAX-mounted ext4, false on tmpfs.

---

## Chapter 4: Filesystems

### The idea

A storage device (a disk, an SSD, a pmem stick) is just a sequence of fixed-size blocks. A **filesystem** imposes structure on those blocks: directories with names, files with sizes, metadata like timestamps and permissions. The filesystem is the layer between "raw bytes on a device" and "files in folders that applications see."

### Simple example

When you run:

```bash
echo "hello" > /tmp/greeting
```

Here's what happens at the filesystem level:

1. Resolve `/tmp` → its directory data
2. Allocate a new inode for `greeting`
3. Allocate free blocks for the data (one block here, since 6 bytes is tiny)
4. Write "hello\n" to those blocks
5. Update the inode's size, mtime, block pointers
6. Add `("greeting", inode_number)` to `/tmp`'s directory entry list
7. Update `/tmp`'s mtime

Six conceptual operations for one tiny file. The filesystem orchestrates all of them and keeps the metadata consistent.

### Inodes

The metadata for a single file is called an **inode**. It contains:

- File size
- Owner, group, permission bits
- Access/modification/creation times
- A list of block pointers (or extents) where the file's data lives

What an inode does *not* contain: the file's name. Names live in directory entries, which map a string to an inode number. This is how **hard links** work — two directory entries point at the same inode. They look like two files but share contents and metadata; the inode has a reference count, and deletion only frees the data when the count hits zero.

### Directory entries and path resolution

A directory is itself a file. Its data is a list of `(name, inode_number)` entries. Resolving a path like `/home/kidus/foo.c` requires walking the chain:

1. Start at the root inode (always inode 2).
2. Read root's data to find `home` → its inode.
3. Read `home`'s data to find `kidus` → its inode.
4. Read `kidus`'s data to find `foo.c` → its inode.
5. Read `foo.c`'s inode for block pointers.
6. Read the data blocks.

The kernel caches these lookups in the **dentry cache** so repeated resolution is fast.

### Journaling

A crash mid-write can leave filesystem metadata inconsistent — a file's size updated but its new blocks not yet linked, etc. A **journal** prevents this by writing a description of the intended change to a circular log *before* applying it. After a crash, the journal is replayed on recovery, either completing or rolling back interrupted operations.

ext4 journals metadata by default. With `data=journal`, data writes are also journaled (safe but slow). With `data=writeback`, only metadata is journaled (fast but a crash can expose stale data inside files).

### Key filesystems

| Filesystem | Notes |
|---|---|
| **ext4** | Default on Ubuntu. Journaled. Supports DAX. Solid general use. |
| **xfs** | High performance, especially for large files and parallel I/O. Also DAX-capable. |
| **tmpfs** | RAM-backed, lives entirely in DRAM (and swap). Disappears on reboot. No disk I/O. |
| **NOVA** | Research filesystem designed specifically for NVM. Not upstream. |
| **btrfs** | Copy-on-write filesystem. Snapshots, checksums. More complex. |

### Block devices

A **block device** is a kernel abstraction over any storage medium that supports reads and writes in fixed-size chunks called blocks (typically 512 bytes or 4 KB). The kernel exposes every block device under `/dev/`, regardless of the underlying hardware. Filesystems are built on top of block devices.

| Device | What it represents |
|---|---|
| `/dev/sda`, `/dev/nvme0n1` | Physical HDD / SSD |
| `/dev/pmem0` | NVM namespace in `fsdax` mode |
| `/dev/loop0` | Virtual block device backed by a regular file |

The loop device is how you "mount a file" (e.g., an ISO image): the kernel creates `/dev/loop0` as a virtual block device, and the loop driver translates each block read/write into a `pread`/`pwrite` on the backing file.

### Where this matters for pmem

Our pmem region appears as `/dev/pmem0` — a block device — even though the underlying NVM is byte-addressable. The block device interface is how the filesystem layer discovers the device's size and lays out its on-disk structures. We formatted it with ext4:

```bash
sudo mkfs.ext4 -b 4096 -E stride=512 -F /dev/pmem0
```

`mkfs.ext4` wrote ext4's superblock, group descriptors, inode tables, and journal onto the NVM as if it were any other block device. The filesystem doesn't know or care that the underlying medium is byte-addressable; that distinction only matters when DAX kicks in at the `mmap` layer (Chapter 7).

Inodes work the same way they do on disk. When you create `/mnt/pmem-emu/persistent.pool`, ext4 allocates an inode in its inode table on the pmem device. The inode's block pointers reference NVM blocks. The journal protects metadata operations against crash. From ext4's perspective, this is just another disk.

---

## Chapter 5: Mounting

### The idea

A filesystem on a device doesn't appear in your file tree until you **mount** it. Mounting attaches the device's directory structure to the global tree at a specific path called a **mount point**, after which paths under the mount point resolve through the mounted filesystem.

### Simple example

You plug in a USB drive. The kernel sees the new device as `/dev/sdb1`. But `ls /dev/sdb1` doesn't show files — it's a block device, just raw bytes.

```bash
sudo mkdir /mnt/usb
sudo mount /dev/sdb1 /mnt/usb
ls /mnt/usb    # now shows the USB's contents
```

The `mount` command reads the filesystem structure on the device, integrates it with the kernel's directory tree at `/mnt/usb`, and from that point on, any path starting with `/mnt/usb/...` is resolved through the new filesystem.

`umount /mnt/usb` reverses it. The directory tree on the USB still exists on disk; it's just no longer reachable from the OS's tree.

### What mounting actually does

Two jobs combined:

1. **Namespace integration** — splices the device's directory tree into the global one at the mount point.
2. **Policy installation** — every mount carries flags that govern all I/O to files on that device: `ro` (read-only), `noexec` (binaries on this filesystem can't be executed), `sync` (every write flushes immediately), and many more.

Mount flags are checked on every operation. Once mounted with `ro`, no process can write to the filesystem, regardless of file permissions.

### Mounting a file via loop device

When you "mount a file" (e.g., a disk image or ISO):

```bash
sudo mount -o loop disk.img /mnt/img
```

The kernel doesn't mount the file directly. It creates `/dev/loop0` backed by `disk.img`, then mounts `/dev/loop0` at `/mnt/img`. The loop driver translates every block read/write on the loop device into a `pread`/`pwrite` on the backing file. The filesystem layer sees a normal block device; it never knows the blocks come from a file.

### fstab

To make a mount persist across reboots, add it to `/etc/fstab`:

```
/dev/sdb1   /mnt/usb   ext4   defaults   0   2
```

`mount -a` reads `fstab` and mounts everything listed. The boot process does this automatically for the entries marked for auto-mount.

### Where this matters for pmem

Our setup:

```bash
sudo mkdir -p /mnt/pmem-emu
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
sudo chown $USER /mnt/pmem-emu
```

This mounts the ext4 filesystem on `/dev/pmem0` at `/mnt/pmem-emu`, with the critical mount flag `-o dax`. Once mounted, the path `/mnt/pmem-emu/persistent.pool` resolves to a file on the pmem device, and any `mmap` of that file uses the DAX code path instead of the page cache (Chapter 7).

The mount does *not* survive reboot — `/dev/pmem0` itself is recreated each boot from the `memmap` kernel parameter (Chapter 9), but the mount is a runtime state and needs to be re-established. Adding to `/etc/fstab` would automate it:

```
/dev/pmem0  /mnt/pmem-emu  ext4  dax  0  0
```

The mount flag `dax` is what changes everything. Without it, even a pmem-backed device routes through the page cache and looks like a regular disk to the application layer.

---

## Chapter 6: Memory-Mapped Files

### The idea

Normally, accessing file contents requires explicit `read()` and `write()` system calls, each of which copies bytes between the kernel's buffers and the user's. **Memory mapping** (`mmap`) eliminates this. It maps a file (or a portion of one) into the process's virtual address space, so the file's bytes appear as ordinary memory locations the process can read and write with pointer dereferences.

### Simple example

```c
int fd = open("data.bin", O_RDWR);
struct stat st;
fstat(fd, &st);
void *p = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Now read/write the file as if it were memory
char c = ((char *)p)[100];   // read byte 100 of the file
((char *)p)[200] = 'X';      // write byte 200 of the file

munmap(p, st.st_size);
close(fd);
```

After `mmap`, the file's contents are addressable. Reads dereference normally. Writes go to the page cache and get propagated to the file lazily (or immediately, if you call `msync`).

### What happens under the hood

`mmap` returns immediately without reading the file. It just sets up page table entries marked "not present" pointing at the file's pages. When the process first accesses a page, the hardware triggers a **page fault**; the kernel handler reads the file's data into a page cache page and fixes up the page table to point at it. Subsequent accesses to the same page are normal memory accesses with no kernel involvement.

This **lazy loading** is part of why `mmap` is fast for random access. You don't pay for pages you don't touch.

### Why use mmap

- **No syscall overhead per access** after the initial fault. For random access across a large file, this beats repeated `pread` calls by orders of magnitude.
- **Shared mappings between processes** literally share physical pages — efficient IPC, zero-copy.
- **Simpler code** — you treat the file as memory, no buffer management.

### MAP_SHARED vs MAP_PRIVATE

- `MAP_SHARED`: writes go back to the file. Multiple processes mapping the same file see each other's writes.
- `MAP_PRIVATE`: copy-on-write. Writes are private to this process; the underlying file is never modified.

`MAP_SHARED` is what PMDK uses — it must see writes propagate to the file (otherwise persistence doesn't work).

### msync

For `MAP_SHARED`, `msync(addr, len, MS_SYNC)` flushes dirty pages back to the file, analogous to `fsync` for a write-based file. On a normal filesystem, the dirty data is in the page cache; `msync` triggers writeback to disk. On a DAX filesystem, there's no page cache copy — `msync` triggers CPU cache flushes instead.

### Where this matters for pmem

PMDK is built almost entirely on `mmap`. When you call `pmemobj_open(path, layout)`, here's what happens:

1. PMDK `open()`s the pool file.
2. It `mmap`s the entire file with `MAP_SHARED`.
3. It returns a pool handle. From this point on, all access to objects in the pool is through pointers into the mapping.

`pmemobj_alloc` carves bytes out of the pool by manipulating an in-pool free list, then returns a `PMEMoid` (an offset into the pool). `pmemobj_direct(oid)` converts the offset to a virtual address by adding the pool's mmap base — giving you a `T*` that points into the mapped file.

This is the bridge between "file on disk" and "objects in memory." There is no separate "load from disk" step at allocation time; everything is in the mapping. The first access to a page faults it in (DRAM cache) or maps it directly (DAX).

Our `pmem_allocator.hpp` is built on this: `pmem_alloc` calls `pmemobj_alloc`, which returns an oid, and we hand back `pmemobj_direct(oid)` — a pointer into the mmap'd region. The user can treat it as ordinary memory.

---

## Chapter 7: DAX: Direct Access

### The idea

For files on a regular disk, the page cache is necessary — the disk is slow, and caching gets reads and writes to memory speed. But for files on NVM, the page cache is wasteful: NVM is *already* at memory speed and byte-addressable. Copying NVM contents into a DRAM page cache page just to access them adds latency and doubles memory consumption. **DAX** (Direct Access) is a filesystem feature that eliminates the page cache for pmem-backed files.

### The problem DAX solves

Without DAX, `mmap`ing a file on any filesystem produces this chain:

```
NVM block on device → page cache page in DRAM → process page table → virtual address
```

Every access to the file goes through a DRAM intermediary. Writes have to be flushed from the page cache back to NVM by `msync`. Reads consume DRAM to hold a copy of NVM that's already fast.

### What DAX does

With DAX, the chain becomes:

```
NVM physical address → process page table → virtual address
```

The filesystem driver, knowing the backing device is byte-addressable NVM, installs page table entries that point *directly* at NVM physical addresses. There's no DRAM copy. A load instruction reads from NVM; a store writes to NVM. The CPU's normal cache hierarchy (L1/L2/L3) is in front of NVM as usual, but the page cache layer is gone.

Durability is achieved with CPU cache flush instructions:

- `clflush addr` / `clwb addr`: write back the cache line containing `addr` from CPU caches to memory.
- `sfence`: ensure all preceding stores complete before any later ones.

Together, `clwb + sfence` is the moral equivalent of `msync` but vastly faster — it's a couple of CPU instructions instead of a syscall and a writeback round-trip.

### Enabling DAX

DAX is enabled at mount time:

```bash
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
```

Without `-o dax`, even a pmem-backed device routes through the page cache. The mount flag is the configuration gate that changes the entire I/O path.

### Filesystem support

Not every filesystem supports DAX — it needs special code in the filesystem driver to handle the direct-mapping path. Supported on Linux: ext4, xfs. Not supported: btrfs, zfs (currently). The research filesystem NOVA was designed specifically for NVM and supports DAX natively.

### How PMDK detects DAX

When PMDK opens a pool file, it calls `pmem_is_pmem(addr, len)`. The function inspects `/proc/self/maps` and the device's sysfs attributes to determine whether the mapping is DAX-backed.

- If yes: PMDK uses `clwb + sfence` for stores.
- If no: PMDK falls back to `msync`.

The user-facing API is identical; only the internal code path changes. Performance and durability semantics, however, are radically different.

### Where this matters for pmem

This is the property that justified our entire `memmap`-based environment setup. We had two options for pmem emulation:

1. **tmpfs** — easy, no GRUB changes. But `pmem_is_pmem()` returns false on tmpfs (it's not a DAX filesystem), so PMDK falls back to `msync`. We'd be developing against the wrong code path.
2. **`memmap=4G!4G` + ext4 + `-o dax`** — requires GRUB edit and reboot, but `pmem_is_pmem()` returns true. PMDK uses the real `clwb + sfence` code path.

We chose (2). When Phase 2 lands and we add `pmem_persist(&contents, sizeof(T))` to our `store()`, we'll actually be exercising the durability instructions, not an `msync` substitute.

---

## Chapter 8: Persistent Memory (NVM/pmem)

### The idea

For decades, computer storage has been divided into two categories: volatile memory (DRAM, fast, byte-addressable, loses contents on power loss) and persistent storage (disk, slow, block-addressable, survives power loss). **Non-Volatile Memory** (NVM, or persistent memory, or pmem) collapses the distinction. It's byte-addressable and sits on the memory bus like DRAM, but it retains data across power loss like a disk.

### Hardware

The commercial example is Intel Optane DC, plugged into DDR memory slots. There's also active research and emerging products around STT-MRAM, ReRAM, and CXL-attached memory. All share the key properties: byte-addressable load/store access, latency in the few-hundred-nanoseconds range (between DRAM and SSD), persistence across power loss.

### The crash consistency problem

NVM gives you durability — but durability is more complicated than it sounds.

Consider a stack push operation:

```cpp
node->value = x;
node->next = top;
top = node;
```

Three writes. On DRAM, a crash before any of them would have no effect (the stack pre-existed; you just lose progress). On NVM, a crash *between* writes leaves the data in an inconsistent state that is now *permanent*:

- Crash after writing `node->next = top` but before `top = node`: the new node is initialized but unreachable.
- Crash after writing `top = node` but before its other fields: `top` points at uninitialized garbage.

Worse, the CPU cache reorders writes. The store buffer might commit `top = node` before `node->next = top`, so even program order isn't preserved at the NVM medium.

This is the **crash consistency problem**: how do you guarantee that the data on NVM is always in a valid state, even after a crash mid-operation?

### Solutions

The academic literature proposes three main approaches:

| System | Approach | Trade-off |
|---|---|---|
| **Mnemosyne** (ASPLOS '11) | Explicit `atomic{}` blocks, redo logging via STM. Every persistent store must be inside a transaction. | Moderate overhead, explicit programming model. |
| **Atlas** (OOPSLA '14) | Lock-inferred failure-atomic sections, undo logging. Existing critical sections automatically become atomic. | Moderate overhead, near-transparent to programmer. |
| **Clobber-NVM** (ASPLOS '21) | Recovery-via-resumption. Only logs the writes that overwrite transaction inputs. | Lowest runtime overhead, heaviest compiler work. |

All three sit on top of low-level primitives:

- **Flush** (`clwb`, `clflush`): push a cache line out to the memory medium.
- **Fence** (`sfence`): ensure preceding stores complete before later ones.
- **Logging**: write a record of intended changes before applying them, so a crash can be reconciled.

### PMDK

PMDK (Persistent Memory Development Kit, from Intel) is the standard library. The relevant components:

- **libpmem** — raw flush/drain primitives. `pmem_persist(addr, len)` is a `clwb` loop plus `sfence`.
- **libpmemobj** — transactional object store. Manages a pool of persistent objects, encodes pointers as `PMEMoid` (pool_uuid + offset), provides transactions for crash consistency.
- **libpmemobj-cpp** — C++ header-only wrappers. `pmem::obj::persistent_ptr<T>`, `pmem::obj::transaction::run(...)`, `pmem::obj::p<T>`.

### PMEMoid vs raw pointers

`pmemobj_alloc` returns a `PMEMoid`, not a `T*`:

```c
struct PMEMoid { uint64_t pool_uuid_lo; uint64_t off; };
```

The `off` field is the byte offset into the pool file. Because it's an offset rather than a virtual address, it's stable across reboots — the same offset means the same data forever. The pool_uuid_lo identifies which pool the object belongs to.

To use a `PMEMoid`, you convert to a raw pointer with `pmemobj_direct(oid)`. This adds the pool's current mmap base to the offset. The resulting pointer is valid only for the current process run — if you persist the raw pointer and restart, it's garbage. If you persist the `PMEMoid` and restart, it still points to the right object.

### Where this matters for `persistent<T>`

The HANDOFF.md notes a deferred decision: how should `persistent<T>` represent pointers internally? Two options:

- **(a) Convert at every use**: store offsets internally, call `pmemobj_direct` whenever the user accesses. Pointer values change each run, but the data structure survives reliably.
- **(b) Stable virtual addresses**: arrange for the pool to always mmap at the same base, so raw pointers are reusable across runs.

The numa-lib analog doesn't have this problem because numa<T> doesn't persist — pointers don't outlive the process. For `persistent<T>`, the choice is fundamental and affects how `persistent_ptr` (if we add one) is implemented.

We deferred this decision to phase 3 of the plan. For phase 1, our `persistent<T>` just uses raw pointers — they happen to point into mmap'd pmem because that's where `operator new` allocates from, but we make no guarantees about cross-run pointer stability.

---

## Chapter 9: GRUB and Kernel Boot Parameters

### The idea

When a computer powers on, the firmware (BIOS or UEFI) finds a bootable disk, loads a small program called a **bootloader**, and runs it. The bootloader's job is to find the operating system kernel, load it into memory, pass it a configuration string, and jump to it. On most Linux systems the bootloader is **GRUB** (Grand Unified Bootloader). The configuration string is the **kernel command line**, and it includes the **kernel parameters** that configure how the kernel sets itself up.

### Simple example — boot sequence

1. Power on. UEFI runs its own initialization (POST, device enumeration), finds the EFI partition on the boot disk.
2. UEFI loads `/boot/efi/EFI/ubuntu/grubx64.efi` (or similar) — that's GRUB.
3. GRUB reads its config from `/boot/grub/grub.cfg`, optionally shows a menu, picks an entry.
4. GRUB loads the kernel (`/boot/vmlinuz-*`) and initramfs (`/boot/initrd.img-*`) into memory.
5. GRUB constructs the kernel command line, transfers control to the kernel entry point.
6. The kernel reads the command line, initializes subsystems (drivers, memory, filesystems), mounts the root filesystem, starts `init` (or `systemd`).

The kernel command line is how you tell the kernel about hardware quirks, debug flags, memory layout overrides — the things that have to be in place before any normal config files are even readable.

### Configuring kernel parameters

You don't edit `/boot/grub/grub.cfg` directly — it's auto-generated. The source of truth is `/etc/default/grub`:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
```

Edit this file, save, then run:

```bash
sudo update-grub
```

`update-grub` regenerates `grub.cfg`. Changes take effect on the next reboot. You can verify which command line booted by reading `/proc/cmdline`.

### Useful kernel parameters

| Parameter | Effect |
|---|---|
| `quiet` | Suppress most boot messages |
| `splash` | Show the boot splash image |
| `single` | Boot to single-user mode (recovery) |
| `mem=4G` | Limit RAM seen by the kernel to 4 GB |
| `nokaslr` | Disable kernel address space randomization |
| `memmap=...` | Reserve a memory range and/or mark it as a special type |

The kernel reads each parameter and configures accordingly. Many drivers also look for module-specific parameters (e.g., `i915.modeset=0`).

### The `memmap` parameter

The `memmap` parameter has several forms; the one relevant for pmem emulation is:

```
memmap=SIZE!START
```

For example, `memmap=4G!4G` says: take 4 GB of RAM starting at physical address 4 GB and expose it to the kernel as a persistent-memory region instead of normal DRAM. The `!` syntax is the part that signals "treat this as pmem"; other `memmap` forms are used for bad-RAM marking or reserving ranges from the kernel entirely.

After boot with this parameter, the kernel's `libnvdimm` subsystem creates a region device under `/sys/bus/nd/devices/region0/`, and the `ndctl` userspace tool can manage namespaces on top of it. The kernel may also auto-create a default namespace in `fsdax` mode, producing `/dev/pmem0`.

### Namespace modes

`ndctl` lets you carve a pmem region into one or more **namespaces**, each with a mode:

- **fsdax**: exposes the namespace as a block device (`/dev/pmem0`). You format it with a filesystem and mount it. Filesystems can opt into DAX. This is what we want.
- **devdax**: exposes the namespace as a character device (`/dev/dax0.0`). Applications mmap it directly with no filesystem in the way. Used by databases that manage their own on-pmem layout.
- **sector**: legacy block-device mode, no DAX, behaves like a slow SSD. For backward compatibility.

For PMDK's libpmemobj (which our project uses), **fsdax** is the right choice.

### Where this matters for pmem

Our environment setup was:

1. Edited `/etc/default/grub`, changed `GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"` to `GRUB_CMDLINE_LINUX_DEFAULT="quiet splash memmap=4G!4G"`.
2. `sudo update-grub`.
3. Rebooted.
4. After reboot, `/sys/bus/nd/devices/region0/` existed. The kernel had auto-created `namespace0.0` in fsdax mode, exposing `/dev/pmem0` (4 GB).
5. `sudo mkfs.ext4 -b 4096 -E stride=512 -F /dev/pmem0` — wrote an ext4 filesystem onto the pmem device.
6. `sudo mount -o dax /dev/pmem0 /mnt/pmem-emu` — mounted with DAX.

The `memmap` parameter is what makes the whole thing exist. Without it, the kernel would treat that DRAM range as ordinary memory, no pmem region would be created, no `/dev/pmem0` would appear, and there'd be nothing to mount. The decision to make this a boot-time parameter (rather than a runtime one) is because the kernel's memory-management subsystem has to know about pmem regions before it starts using memory — you can't carve out 4 GB of DRAM as pmem after applications are already running.

This is also why the pmem region survives reboots even though we used DRAM to back it: the kernel reserves the same physical range every boot, and the data the previous boot wrote to that range happens to still be there. (For real Optane DIMMs the durability is in the hardware, not in DRAM reservation; the principle is the same from the application's perspective.)
