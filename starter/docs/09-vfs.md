# Step 9: scoped VFS file objects and a volatile RAM filesystem

## 1. Alternatives

A global path-based VFS is familiar, but implicitly exposes a namespace unless all
resolution is scoped. Directory capabilities make the starting authority explicit.
A disk filesystem supplies persistence but needs a block driver and crash-consistent
metadata. RAMFS gives deterministic functional tests first. A user-mode filesystem
server fits the project's hybrid goal; an in-kernel reference backend is simpler
to bootstrap before multi-process IPC and server lifecycle are implemented.

## 2. Recommendation

Use capability-scoped directory resolution and a backend-neutral file operation
vtable, initially backed by RAMFS. The RAMFS here is **in kernel space**. This is
a documented prototype exception to the desired hybrid architecture, not a claim
that the user-mode filesystem service goal has been reached. Do not add disk
persistence or pretend that volatile commits survive power failure.

## 3. Specification

```c
struct vfs_operations {
    int64_t (*read)(struct object *, void *, size_t, size_t *);
    int64_t (*write)(struct object *, const void *, size_t, size_t *);
    int64_t (*seek)(struct object *, uint64_t);
};
struct vfs_file { struct object object; const struct vfs_operations *operations; };
```

VFS checks type and READ/WRITE before dispatching to trusted methods. Seek changes
only a local open-description offset and requires a valid FILE handle. Duplicates
share that offset; separate opens do not. Pools are bounded: eight files, 2048 bytes
per file, sixteen open descriptions per namespace. Each directory capability
refers to one independently initialized RAMFS namespace. Root storage is permanent
and retains a creator reference; open descriptions are reclaimed at last close.

Names are explicit-length relative leaf names, 1..31 ASCII letters/digits/underscore/
hyphen/dot. Slash, NUL, absolute paths, dot and dot-dot are rejected. No symlinks,
mount traversal or directories within a namespace exist. Open requires directory
READ for resolution plus all requested child rights; create also requires WRITE.
Requested file rights are limited to READ/WRITE/DUPLICATE/TRANSFER. Unlink requires
directory WRITE and rejects open files with EBUSY. Creation has no side effect if
there is no node, description or handle slot available. A missing name is ENOENT.
Writes are all-or-nothing at the 2048-byte limit; seeks beyond EOF create zero-filled
gaps on a later write. Reads at EOF return 0. Reboot loses every file.

Scalar calls (all unspecified arguments are zero):

```text
7  FS_OPEN(directory, name_pointer, name_length, rights, create_boolean) -> handle
8  FS_READ(file, buffer, length) -> bytes_read
9  FS_WRITE(file, buffer, length) -> bytes_written
10 FS_SEEK(file, absolute_offset) -> 0
11 FS_UNLINK(directory, name_pointer, name_length) -> 0
```

The single syscall transfer limit is 2048 bytes. Entry copies names/input bytes
into kernel-owned storage and validates the entire output range before advancing
offsets or consuming data. Errors return zero value. No user pointer becomes a
backend or kernel object pointer. File methods cannot sleep/yield in this initial
non-preemptible syscall context.

## 4. Working code

kernel/vfs.h and vfs.c implement common file dispatch. fs.h/fs.c implement RAMFS
and fs_test.c supplies the compiled fixture. services.c owns user-copy policy.
The kernel loader explicitly supplies a directory capability at launch; there is
no get-root syscall, implicit working directory, ambient device or console access.

```c
/* Trusted setup only; namespace storage must outlive every derived file. */
fs_init(&filesystem);
fs_root(&filesystem, &table, CAP_ALL, &directory);
/* Clients use explicit directory capabilities, not a globally authorized path. */
```

## 5. Tests

`make test-extended` checks creation, independent cursors, reads/writes, zero-filled
gaps, denied writes, busy unlink, deletion and missing paths in host and guest.
The real user demo exercises these through INT80 and the compatibility library.
`make test` inspects post-process page/handle cleanup and the earlier fault suite.

## 6. Risks and limits

The directory backend is currently RAMFS-specific, while FILE operations have a
common VFS interface. There is no disk, journal, full mount routing, fsync, directory
listing, permission ACL, truncate, links, mmap, or network filesystem. Type tags do
not authenticate a forged kernel vtable; all creators remain trusted kernel code.
Moving this reference backend into userspace requires IPC, crash recovery and
capability-preserving request routing; typed data framing alone does not provide
that service architecture. Small limits are explicit errors, not silent truncation.
