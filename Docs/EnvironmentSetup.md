# Environment Setup: Persistent Memory Emulation

This document records exactly what was done to prepare this machine for persistent memory development using PMDK, and why each step was necessary.

## Machine

- **OS:** Ubuntu 22.04.5 LTS
- **Kernel:** 6.8.0-59-generic
- **RAM:** 32 GB DRAM
- **NVM:** None (no Optane DIMMs). Full emulation via `memmap` kernel parameter.
- **Compilers:** GCC 11.4, Clang 20.0

---

## Step 1: Install PMDK

```bash
sudo apt install -y libpmem-dev libpmemobj-dev libpmemobj-cpp-dev
```

### What was installed

| Package | Version | Purpose |
|---------|---------|---------|
| `libpmem-dev` | 1.11.1 | Low-level pmem primitives: `pmem_flush`, `pmem_drain`, `pmem_memcpy`. The foundation. |
| `libpmemobj-dev` | 1.11.1 | Object store built on libpmem. Provides the pool API: `pmemobj_create`, `pmemobj_open`, `pmemobj_alloc`, `pmemobj_root`. This is what `PersistentAllocator` will use. |
| `libpmemobj-cpp-dev` | 1.13.0 | Header-only C++ wrappers: `pmem::obj::pool<T>`, `pmem::obj::persistent_ptr<T>`, RAII transaction helpers. |

Note: `pmempool` (the pool inspection utility) is not available via apt — it ships inside the PMDK source tree. Not needed for phase 1.

### Why not build PMDK from source

The apt packages are sufficient for phases 1–4. The prior development machine used a source build only because `pmempool` was needed. On this machine we skip that.

---

## Step 2: Kernel pmem emulation via `memmap`

### Why not use tmpfs

The initial plan was to use tmpfs:

```bash
sudo mount -t tmpfs -o size=4G tmpfs /mnt/pmem-emu
```

tmpfs works and PMDK's API behaves identically. However, `pmem_is_pmem()` returns **false** on tmpfs, which means PMDK falls back to `msync` rather than using `clflush + sfence`. For a library whose phase 2 goal is explicit flush/fence semantics, testing against `msync` hides the real code path. The `memmap` approach was chosen instead.

### GRUB edit

Edited `/etc/default/grub`, changing:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
```

to:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash memmap=4G!4G"
```

`memmap=4G!4G` reserves 4 GB of DRAM starting at physical address 4 GB and tells the kernel's `libnvdimm` subsystem to expose it as a persistent memory region rather than normal RAM.

Then:

```bash
sudo update-grub
sudo reboot
```

### Post-reboot state

After reboot, the kernel auto-created a namespace in `fsdax` mode. `sudo ndctl list -RN` showed:

```json
{
  "dev": "region0",
  "size": 4294967296,
  "available_size": 0,
  "type": "pmem",
  "namespaces": [
    {
      "dev": "namespace0.0",
      "mode": "fsdax",
      "size": 4294967296,
      "blockdev": "pmem0"
    }
  ]
}
```

`/dev/pmem0` was already present. The `ndctl create-namespace` step from the original plan was unnecessary — the kernel had done it automatically.

---

## Step 3: Format and mount with DAX

```bash
sudo mkfs.ext4 -b 4096 -E stride=512 -F /dev/pmem0
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
sudo chown $USER /mnt/pmem-emu
```

The `-o dax` mount flag is critical. It bypasses the page cache and maps pmem addresses directly into process virtual address space via `mmap`. Without it, `pmem_is_pmem()` returns false even on a real pmem device.

Verify:

```bash
mount | grep pmem
# expected: /dev/pmem0 on /mnt/pmem-emu type ext4 (rw,relatime,dax)
```

---

## Current state

- `/mnt/pmem-emu` — 4 GB ext4 filesystem, DAX-mounted, owned by `$USER`
- `pmem_is_pmem()` returns **1** for files created here
- PMDK will use `clflush + sfence` (not `msync`) for durability operations
- Pool files for development go under `/mnt/pmem-emu/`

The default pool path for the `persistent<T>` library (phase 1) will be `/mnt/pmem-emu/persistent.pool`, overridable via the `PERSISTENT_POOL_PATH` environment variable.

---

## Re-mount after reboot

The tmpfs mount from step 2's earlier attempt does not survive reboot. The `/dev/pmem0` device persists (it is recreated from the kernel parameter at each boot), but the mount does not. After any reboot:

```bash
sudo mount -o dax /dev/pmem0 /mnt/pmem-emu
```

To make this permanent, add to `/etc/fstab`:

```
/dev/pmem0  /mnt/pmem-emu  ext4  dax  0  0
```
