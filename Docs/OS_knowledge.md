# OS Knowledge Base: Filesystems, Paging, and Memory

A running reference for concepts that come up in systems interviews and in this project. Written to build understanding from the ground up — read top to bottom once, then use as a lookup.

---

## Table of Contents

1. [Memory Hierarchy](#1-memory-hierarchy)
2. [Virtual Memory and Paging](#2-virtual-memory-and-paging)
3. [The Page Cache](#3-the-page-cache)
4. [Filesystems](#4-filesystems)
5. [Mounting](#5-mounting)
6. [Memory-Mapped Files](#6-memory-mapped-files)
7. [DAX: Direct Access](#7-dax-direct-access)
8. [Persistent Memory (NVM/pmem)](#8-persistent-memory-nvmpmem)
9. [GRUB and Kernel Boot Parameters](#9-grub-and-kernel-boot-parameters)

---

## 1. Memory Hierarchy

Every modern system has a hierarchy of storage, ordered by speed and proximity to the CPU:

```
Registers       ~1 cycle        a few hundred bytes
L1 cache        ~4 cycles       32–64 KB per core
L2 cache        ~12 cycles      256 KB – 1 MB per core
L3 cache        ~40 cycles      8–64 MB shared
DRAM            ~100 cycles     GBs
NVM (Optane)    ~300 cycles     GBs – TBs, survives power loss
SSD             ~100,000 cycles TBs
HDD             ~10,000,000     TBs
```

The key insight: the further from the CPU, the slower and cheaper per byte. The OS exists largely to manage this hierarchy — keeping hot data close to the CPU and hiding the latency of slower tiers from applications.

**Cache lines** are the unit of transfer between levels. On x86, a cache line is 64 bytes. When you access one byte of DRAM, the CPU fetches the entire 64-byte cache line. This is why spatial locality matters: accessing array elements sequentially is fast because they share cache lines.

---

## 2. Virtual Memory and Paging

### The problem virtual memory solves

Without virtual memory, every program would need to know where in physical RAM it would be loaded, programs couldn't use more memory than physically available, and one program's bug could corrupt another program's memory. Virtual memory solves all three.

### Address spaces

Every process has its own **virtual address space** — a range of addresses the process can use (0 to 2^48 on a typical 64-bit Linux system). These are not physical addresses. The CPU's **Memory Management Unit (MMU)** translates virtual addresses to physical addresses on every memory access.

### Pages and page tables

The address space is divided into fixed-size chunks called **pages** (4 KB on x86 by default, also 2 MB and 1 GB huge pages). Physical memory is divided into **frames** of the same size.

The **page table** is a data structure (maintained by the OS, walked by the MMU) that maps virtual page numbers to physical frame numbers. Each entry also contains permission bits (readable, writable, executable) and a **present bit** indicating whether the page is currently in physical memory.

On x86-64, the page table is four levels deep (PGD → PUD → PMD → PTE). A full walk takes four memory accesses — expensive, which is why the TLB exists.

### TLB (Translation Lookaside Buffer)

The TLB is a small, fast hardware cache inside the MMU that stores recent virtual-to-physical translations. A TLB hit costs ~1 cycle. A TLB miss triggers a hardware page table walk (~40–100 cycles). A full page table walk that finds the page is not present triggers a **page fault**, handled in software by the OS.

**TLB shootdown:** when the OS changes a page table entry (e.g., during `munmap`), it must invalidate the corresponding TLB entry on every CPU that might have cached that translation. This requires an inter-processor interrupt (IPI) and is one of the more expensive operations in the kernel.

### Page faults

A page fault occurs when the MMU cannot complete a translation:

- **Minor fault:** the page exists in memory but the page table entry is missing (e.g., copy-on-write). Resolved without disk I/O.
- **Major fault:** the page was swapped to disk or has never been loaded. The OS must fetch it from disk, install it in a frame, update the page table, and resume the process. Expensive.
- **Segfault (SIGSEGV):** the address is invalid or the process lacks permission. The OS kills the process (or delivers the signal).

### Copy-on-write (COW)

When `fork()` is called, the OS doesn't copy the parent's memory — it marks all pages in both parent and child as read-only and shared. When either process writes to a page, a fault fires, the OS copies just that page, and both processes get their own copy. This makes `fork()` fast when followed by `exec()` (as in a shell).

---

## 3. The Page Cache

### What it is

The **page cache** is a region of DRAM that the kernel uses to cache file data. When a process reads from a file, the kernel checks the page cache first. If the data is there (**cache hit**), it's copied to the process's buffer without touching the disk. If not (**cache miss**), the kernel reads from disk into the cache, then copies to the process.

The page cache uses all free DRAM — it grows as files are read and shrinks when processes need more heap/stack memory. `free -h` shows this as "buff/cache."

### Write path

By default, writes go to the page cache and are marked **dirty**. The kernel's writeback threads flush dirty pages to disk periodically (default: within 30 seconds) or when dirty data exceeds a threshold. This is why:
- Writes appear fast (they complete once in the cache)
- You can lose data if the machine crashes before writeback

### `fsync` and `fdatasync`

`fsync(fd)` flushes all dirty pages for a file to the underlying device and waits for completion. This is how databases ensure a committed transaction survives a crash. `fdatasync` is like `fsync` but skips updating metadata (timestamps, etc.) — faster when you only care about data integrity.

### `O_DIRECT`

Opening a file with `O_DIRECT` bypasses the page cache entirely. Reads and writes go straight between the process's buffer and the device. Used by databases that implement their own caching (e.g., PostgreSQL, MySQL InnoDB). Requires aligned buffers and aligned sizes.

---

## 4. Filesystems

### What a filesystem does

A filesystem imposes structure on a raw block device — it organizes bytes into files and directories, tracks which blocks belong to which file, manages free space, and provides the names-to-data mapping that applications use.

### Inodes

An **inode** (index node) is a fixed-size metadata record that describes one file or directory. It stores:
- File size
- Owner, permissions, timestamps
- Block pointers: the list of disk blocks (or extents) that hold the file's data

The inode does **not** store the file's name. Names live in directory entries, which map a name string to an inode number. This is how hard links work: two directory entries pointing at the same inode.

### Directory entries and path resolution

A directory is a file whose data is a list of `(name, inode_number)` pairs. Resolving `/home/kidus/foo.c`:
1. Start at the root inode (always inode 2).
2. Read root's data blocks to find the entry for `home` → get its inode.
3. Read `home`'s data blocks to find `kidus` → get its inode.
4. Read `kidus`'s data blocks to find `foo.c` → get its inode.
5. Read `foo.c`'s inode to get its data block addresses.
6. Read data blocks.

The **dentry cache** (dcache) in the kernel caches these path→inode lookups so repeated resolution is fast.

### Journaling

A crash mid-write can leave filesystem metadata inconsistent (e.g., a file's size updated but its new blocks not yet linked). A **journal** (write-ahead log) prevents this: before modifying metadata on disk, the filesystem writes a description of the intended change to a circular log. If the system crashes, the journal is replayed on recovery to either complete or roll back the operation.

ext4 journals metadata by default. With `data=journal`, data writes are also journaled (safe but slow). With `data=writeback`, only metadata is journaled (fast but a crash can expose stale data in a file).

### Key filesystems

| Filesystem | Notes |
|-----------|-------|
| **ext4** | Default on Ubuntu. Journaled. Supports DAX. Solid for general use. |
| **xfs** | High performance, especially for large files and parallel I/O. Also DAX-capable. |
| **tmpfs** | RAM-backed. Lives entirely in DRAM (and swap). Disappears on reboot. No disk I/O. |
| **NOVA** | Research filesystem designed specifically for NVM. Not upstream. |
| **btrfs** | Copy-on-write filesystem. Snapshots, checksums. More complex. |

### Block devices

A **block device** is a kernel abstraction over any storage medium that supports reads and writes in fixed-size chunks called **blocks** (typically 512 bytes or 4 KB). The kernel exposes every block device through a uniform interface under `/dev/`, regardless of the underlying hardware. Filesystems are built on top of block devices — the filesystem interprets the raw blocks as inodes, directory entries, and data.

Common block devices:

| Device | What it represents |
|--------|-------------------|
| `/dev/sda`, `/dev/nvme0n1` | Physical HDD / SSD |
| `/dev/pmem0` | NVM namespace in `fsdax` mode |
| `/dev/loop0` | Virtual device backed by a regular file |

The loop device is how you "mount a file" (e.g., an ISO image): the kernel creates `/dev/loop0` as a virtual block device, and the loop driver translates every block read/write into a `pread`/`pwrite` on the backing file. The filesystem mounted on top sees an ordinary block device.

`/dev/pmem0` is also a block device, even though the underlying NVM is byte-addressable. The block device interface is how the filesystem layer discovers and manages it. The DAX bypass (section 7, DAX: Direct Access) kicks in at the `mmap` level, after the filesystem has already used the block interface to find where file extents live on the device.

---

## 5. Mounting

### What mounting does

Mounting has two jobs:

**Namespace integration** — attach a device's directory tree to the OS's global directory tree at a **mount point**. Before mounting, `/dev/pmem0` is just raw bytes with no accessible paths. After `mount /dev/pmem0 /mnt/pmem-emu`, the kernel reads the filesystem structure from the device and stitches it into the tree so you can `ls /mnt/pmem-emu` and reach actual files.

**Policy installation** — every mount carries flags that govern all I/O to files on that device for the life of the mount: read-only, `noexec`, `sync`, and critically for pmem: `-o dax`.

### Mounting a file (loop device)

When you mount a file (e.g., an ISO), the kernel doesn't mount the file directly. It first creates a loop device backed by the file, then mounts that loop device:

```
/path/to/image.iso  →  /dev/loop0  →  mount  →  /mnt/iso
```

The filesystem layer sees a normal block device; it never knows the blocks come from a file.

### Mounting pmem with `-o dax`

A normal mount routes `mmap` through the page cache:

```
NVM → page cache (DRAM copy) → page table → process virtual address
```

With `-o dax`, the filesystem driver knows the backing device is byte-addressable NVM. On `mmap`, it installs page table entries that point directly at the NVM physical addresses:

```
NVM physical addresses → page table → process virtual address
```

No DRAM copy. A `movq` instruction hits NVM directly. The mount flag is the configuration gate that changes the entire I/O path for every file on that device.

```bash
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
```

Without `-o dax`, even real NVM goes through the page cache and `pmem_is_pmem()` returns false — PMDK falls back to `msync` instead of `clwb + sfence`.

---

## 6. Memory-Mapped Files

### `mmap`

`mmap(2)` maps a file (or a portion of one) directly into a process's virtual address space. After the call, the process can read and write the file using ordinary pointer dereferences — no `read()`/`write()` syscalls needed.

Under the hood, `mmap` sets up page table entries that point at the file's pages in the page cache. The first access to each page triggers a minor page fault, which the kernel resolves by installing the mapping. Subsequent accesses are as fast as any other memory access.

### Why use `mmap`

- **No syscall overhead per access** after the initial mapping. For random access patterns across a large file, this beats repeated `pread` calls.
- **Shared mappings** between processes share physical pages — efficient IPC.
- **PMDK uses `mmap` for everything.** When you open a pmem pool, PMDK maps the entire pool file into the process's address space. `pmemobj_alloc` returns a pointer into that mapping.

### `MAP_SHARED` vs `MAP_PRIVATE`

- `MAP_SHARED`: writes are visible to other processes mapping the same file, and eventually written back to the file.
- `MAP_PRIVATE`: copy-on-write. Writes are private to this process and never written back to the file.

### `msync`

For a `MAP_SHARED` mmap, `msync(addr, len, MS_SYNC)` flushes dirty pages back to the underlying file, analogous to `fsync`. On a normal filesystem this goes through the page cache. On a DAX filesystem, it triggers CPU cache flushes instead.

---

## 7. DAX: Direct Access

### The page cache is wasteful for pmem

For NVM (persistent memory sitting on the memory bus), routing I/O through the page cache means:
1. Copying data from NVM into a DRAM page cache page.
2. Copying from the page cache page into the process's buffer (or mapping).

Both copies are unnecessary — the NVM is byte-addressable and already fast. The page cache adds latency and wastes DRAM.

### What DAX does

**DAX** (Direct Access) is a filesystem feature that eliminates the page cache for files on pmem-backed devices. With a DAX-mounted filesystem:

- `mmap` maps the NVM physical addresses directly into the process's virtual address space.
- Reads and writes are load/store instructions that go straight to NVM.
- There are no intermediate copies.
- Durability is achieved with CPU cache flush instructions (`clflush`, `clwb`) plus a store fence (`sfence`), not `msync`.

### Enabling DAX

DAX is enabled at mount time:

```bash
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
```

Without `-o dax`, even a pmem-backed device routes through the page cache and `pmem_is_pmem()` returns false.

### How PMDK detects DAX

When PMDK opens a pool file, it calls `pmem_is_pmem()`, which inspects `/proc/self/maps` and the device's sysfs attributes to determine whether the mapping is DAX-backed. If yes, PMDK uses `clwb + sfence` for stores. If no, it falls back to `msync`. The API is identical — the code path is not.

---

## 8. Persistent Memory (NVM/pmem)

### What makes NVM different

DRAM loses its contents when power is removed. NVM (Non-Volatile Memory) — Intel Optane DC being the main commercial example — retains data across power loss, like an SSD, but is byte-addressable and sits on the memory bus (DDR or CXL), like DRAM. This combination is new and requires new programming models.

### The crash consistency problem

With DRAM, a crash means you lose whatever was in memory. With NVM, a crash means a partially-completed operation is permanently visible the next time you boot. A stack's `push` operation might update the `top` pointer but not the new node's contents — and that inconsistency is now durable. This is the **crash consistency problem**.

The three academic approaches to solving it (relevant to this project):

| System | Approach | Overhead |
|--------|---------|---------|
| **Mnemosyne** (ASPLOS '11) | Explicit `atomic{}` blocks, redo logging via STM. Every persistent store must be inside a transaction. | Moderate |
| **Atlas** (OOPSLA '14) | Lock-inferred failure-atomic sections, undo logging. Most transparent — no explicit transactions needed. | Moderate |
| **Clobber-NVM** (ASPLOS '21) | Recovery-via-resumption. Only logs writes that overwrite transaction inputs. Lowest overhead, heaviest compiler work. | Low |

### PMDK

PMDK (Persistent Memory Development Kit, by Intel) is the standard library for pmem programming. The relevant components:

- **libpmem** — raw flush/drain primitives. `pmem_persist(addr, len)` = `clwb` loop + `sfence`.
- **libpmemobj** — transactional object store. Manages a pool of persistent objects, handles `PMEMoid` (pool-id + offset) pointer encoding, and provides transaction APIs for crash consistency.
- **libpmemobj-cpp** — C++ header-only wrappers. `pmem::obj::persistent_ptr<T>`, `pmem::obj::transaction::run(...)`.

### PMEMoid vs raw pointers

`pmemobj_alloc` returns a `PMEMoid`, not a `T*`. A `PMEMoid` is a `{pool_uuid, offset}` pair — stable across reboots because it's an offset, not a virtual address. To get a usable pointer: `pmemobj_direct(oid)` → `T*`. That pointer is only valid for the current process run (virtual addresses change between runs unless the pool is always mapped at the same base address).

---

## 9. GRUB and Kernel Boot Parameters

### Boot sequence overview

1. Power on → firmware (BIOS/UEFI) runs POST, finds a bootable device.
2. GRUB is loaded from the bootloader partition.
3. GRUB reads `/boot/grub/grub.cfg`, presents a menu (if configured), loads the kernel and initramfs into memory.
4. GRUB passes a command-line string to the kernel and jumps to it.
5. The kernel initializes subsystems, mounts the root filesystem, starts `init`/`systemd`.

### Configuring kernel parameters

You do not edit `/boot/grub/grub.cfg` directly — it is auto-generated. The source of truth is `/etc/default/grub`:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
```

After editing this file, run `sudo update-grub` to regenerate `grub.cfg`. Changes take effect on the next boot.

### The `memmap` parameter for pmem emulation

```
memmap=4G!4G
```

Reserves 4 GB of DRAM starting at physical address 4 GB and exposes it to the kernel's `libnvdimm` subsystem as a persistent memory region. The `!` syntax specifically signals pmem; other `memmap` forms are used for bad RAM marking and address reservations.

After boot with this parameter:
- `libnvdimm` creates `/sys/bus/nd/devices/region0`
- `ndctl` can create namespaces within that region
- The kernel may auto-create a namespace in `fsdax` mode, producing `/dev/pmem0`

### ndctl and namespace modes

`ndctl` manages the namespace layer between raw pmem regions and block/character devices:

```bash
ndctl create-namespace --mode=fsdax --region=region0
```

Modes:
- **fsdax** — block device (`/dev/pmem0`), filesystem + DAX. What PMDK's libpmemobj needs.
- **devdax** — character device (`/dev/dax0.0`), direct mmap only.
- **sector** — block device, no DAX, legacy compatibility.
