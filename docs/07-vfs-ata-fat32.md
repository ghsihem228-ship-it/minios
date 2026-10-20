# VFS, ATA and persistent FAT32 (v0.7)

Files under `/disk` survive QEMU restarts. Programs can use the existing file
descriptor ABI on either backend, and the ELF loader reads through the VFS.
The kernel remains single-CPU and non-preemptible while servicing kernel paths.

## Workflow and disk ownership

`make run` invokes `tools/mkdisk.py disks/minios.img` before attaching that image
as primary-master IDE with explicit raw format. The formatter uses exclusive
creation, so it cannot overwrite an existing file or race another creator. Its
default image is a sparse 64 MiB FAT32 superfloppy, with mirrored FATs, a matching
root/boot volume label, valid FSInfo, and backup boot/FSInfo sectors. Sizes
64–512 MiB are supported by the host tool. It needs only Python's standard
library. The kernel never formats media and never opens host paths directly.

`disks/` is ignored by Git and survives `make clean`. Tests create their own
temporary images and never use this development disk. To select another image:

```bash
make run DISK_IMAGE=disks/experiment.img
```

The image is a data volume, not a bootable OS installer. Kernel boot still uses
Multiboot through QEMU `-kernel`. Do not attach a disk image to two writable QEMU
instances simultaneously. To inspect a stopped development image independently:

```bash
fsck.fat -n disks/minios.img
```

`fsck.fat` comes from optional `dosfstools`. Repair, if needed, is an offline
operation; the kernel does not attempt automatic recovery or clearing of dirty
media. Persistent images are not source files and are not pushed to GitHub.

## Block device

`block.c` implements primary-master legacy ATA PIO: IDENTIFY DEVICE, single-sector
READ/WRITE SECTORS and FLUSH CACHE. LBA28 capacity and every requested sector
are checked. Sector size is 512 bytes. ATA interrupts are disabled with nIEN;
status polling has a finite iteration budget and checks ERR/device-fault bits.
No timer-dependent timeout is used inside interrupt-gate syscalls. Absence,
unsupported ATAPI and failed IDENTIFY leave RAMFS available without mounting
a disk. Other channels, slaves, LBA48, DMA, AHCI, NVMe and hotplug are not included.

Long I/O runs with the current non-preemptible kernel rules. This is a simple
correctness-first path, not a high-throughput or latency-optimized storage stack.

## VFS

`vfs.h` owns `file_handle`, flags, stat and backend operations. Handles carry an
opaque backend node, operations table, independent offset and flags. Dispatch
chooses one of two fixed mounts with a path-component boundary: root RAMFS or
`/disk` FAT32. `/diskette` is not routed to the disk. An unavailable `/disk`
returns ENODEV, rather than accidentally creating volatile files there. Paths
are absolute and bounded to 63 characters; dot traversal is rejected.

RAMFS retains its flat absolute-name namespace, read-only built-ins and 4 KiB
mutable files. FAT32 implements only root regular files. These are not general
hierarchical directories or a dynamic mount API. No process scheduling happens
inside a backend operation on this single CPU.

`fs_read_all` loads a bounded image (at most 1 MiB) through common stat/open/read
operations. `task_spawn` frees the temporary buffer and trims excess heap pages
on success and failure. ELF validation and private user mappings remain unchanged.
Thus `cp /bin/hello /disk/hello.elf` followed by `run /disk/hello.elf` exercises
actual disk-backed program loading rather than an alias to the built-in image.

## FAT32 validation and scope

The driver accepts a FAT32 superfloppy or exactly one primary MBR partition of
type 0x0B/0x0C. Sector/volume/partition arithmetic is checked against IDENTIFY
capacity. Required geometry: 512-byte sectors, power-of-two sectors per cluster
from 1 through 64, one or two mirrored FATs, FAT32 cluster count (at least 65,525,
at most 1,048,576), sufficient FAT capacity, valid root and version zero. Active
FAT selection with mirroring disabled, GPT, extended partitions, other FAT
variants and unsupported geometries are rejected. Mount does not modify media.

Both FAT copies are compared during mount's free-space scan and later lookups.
Chain walks use Brent cycle detection and a geometry-based hop bound. File opens
validate chain length and reject cycles or references into the root-directory
chain. The root is limited to 256 clusters. The driver is not a full fsck: it
does not globally prove that two arbitrary existing files have no cross-links.
Use clean, dedicated images, and offline repair for damaged media.

Filenames use a case-insensitive ASCII 8.3 subset (letters, digits, underscore,
hyphen and one extension separator). New names are stored uppercase. Root files
can grow to 16 MiB; up to 32 distinct FAT files may be open concurrently, with
existing per-process descriptor limits unchanged. Long names and subdirectory
operations are not implemented. Long-name records are skipped for listing;
readable supported short aliases are allowed, but writing/deleting an alias
with preceding LFN records returns EOPNOTSUPP to avoid orphaning those records.
Read-only file attributes are enforced. Deleting an open file returns EBUSY.
Directory entries retain valid end markers across sector/cluster boundaries;
deleted slots are reused and the root chain grows as needed. Directory space
is retained after file deletion. Timestamps default to 1980-01-01 until RTC/time
support is added.

## Allocation, durability and errors

Growth reserves a separate zeroed chain before attaching it to a file. ENOSPC
returns the entire reservation, preserving the previous chain/length. Successful
writes handle partial sectors, multiple clusters and zero-filled gaps after
seeking past EOF. Data is flushed before publishing the new directory size and
first cluster. Truncation publishes an empty entry before freeing its old chain;
deletion invalidates the directory entry before releasing data clusters. Active
opens share node metadata, while retaining independent offsets.

Before each mutating operation, both FATs' clean bits are cleared and flushed.
Valid FSInfo summaries are temporarily invalidated. At successful completion
(or successful ENOSPC rollback), primary/backup FSInfo counts are updated from
mount-scanned/incrementally maintained accounting, then the FAT clean bits are
set and flushed. A small read cache covers FAT sectors; there is no filesystem
write-back cache. `sync` explicitly issues FLUSH CACHE as well.

An ATA error or corrupt chain reports EIO and switches the mounted filesystem
to read-only. Subsequent writes fail EROFS before attempting a directory lookup.
An unclean/hard-error FAT flag at boot mounts read-only. Mirror mismatch or
malformed root geometry/chain rejects the mount, leaving RAMFS and the shell
usable. No automatic retry loop, reformat or dirty-bit clearing hides an error.

These are ordered, write-through operations, **not journaled transactions**.
Power loss can still leave partial writes, leaked reservations or mismatched
FATs. Existing data overwrites are not atomic. Clean-bit ordering makes interrupted
operations detectable; it does not repair them. Flush guarantees also depend on
the host/storage honoring FLUSH CACHE. Reboot tests demonstrate persistence
across new QEMU processes, not survival of a physical host power failure.

## Commands and ABI additions

| Command | Behavior |
| --- | --- |
| `df` | Mount/access state, cluster geometry and free space |
| `ls /disk` | FAT root short names and sizes |
| `stat <path>` | Common size and read-only state |
| `write <path> <word>` | Create/truncate and write one shell word |
| `append <path> <word>` | Create if absent, seek to EOF and write |
| `cp <source> <destination>` | Copy through VFS; destination must not exist |
| `rm <path>` | Delete a closed mutable file |
| `sync` | Flush mounted disk; ENODEV without a mounted disk |
| `run /disk/NAME.ELF` | Load and run a persistent ELF32 image |

`cp` is a sequence of writes, not an atomic whole-file operation. On an I/O or
space error a partial new destination may remain. Existing destinations are
refused, including case-insensitive aliases. FAT errors include ENODEV 19,
EEXIST 17, EISDIR 21, EFBIG 27, ENOSPC 28, EROFS 30, ENAMETOOLONG 36,
EOPNOTSUPP 95 and EIO 5. ATA polling timeouts become filesystem EIO.

New `int 0x80` calls use the same register ABI:

| EAX | Call | EBX | ECX |
| --- | --- | --- | --- |
| 12 | unlink | user path | — |
| 13 | sync | — | — |
| 14 | stat | user path | writable 8-byte `{uint32_t size; int32_t readonly;}` |

Path strings and output buffers are checked using the existing user mapping
validation. Console/file I/O stays limited to 1024 bytes per user call. The
`diskdemo` program writes 12 KiB, verifies it, overwrites across a sector boundary,
seeks past EOF to test zero-filled gaps, checks stat/EBUSY, closes and syncs.

## Validation

`make test` runs existing 2/64/128 MiB memory, syscall and process regressions,
then `tests/storage.py`. `make test-storage` runs the storage suite alone. Tests
use temporary images exclusively and print named PASS groups. They cover:

- Multi-sector/cluster data and sparse gap bytes checked by an independent host
  FAT parser, including mirrored FAT equality and no leaked/cross-linked clusters.
- Root growth past a cluster, slot reuse, truncation and freeing deleted chains.
- Three fresh QEMU boots, including 2 MiB and execution of a persisted ELF.
- Dirty read-only volumes, mirror/root/file cycles, invalid geometry and
  unformatted disks; rejected/read-only images remain byte-for-byte unchanged.
- ENOSPC after a partial reservation, with restored free-space accounting.
- Nonzero MBR partition offsets and persistence across another boot.
- QEMU blkdebug write-only and flush-only errors, EIO propagation, read access
  after a failed write and read-only mounting after a failed flush/reboot.
- Stale directory bytes beyond an end marker, LFN alias mutation rejection and
  prevention of file references into the root-directory chain.
- Optional independent `fsck.fat -n` on the successful persistence image.

Python 3.8+ and QEMU with blkdebug I/O-type injection (4.1+) are sufficient. No
network, GUI, KVM or third-party Python packages are needed. `dosfstools` is an
optional independent checker, not a prerequisite for creating or mounting disks.
