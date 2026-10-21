# 17. Running ELF programs from snapshot files

## Design and recommendation

Embedding programs makes early boot deterministic but requires rebuilding the
kernel for each application. Loading saved files supports independent application
builds. A scalable filesystem would remove today's 2 KiB file limit but needs a
separate allocation/recovery design. For now, keep the bounded snapshot filesystem
and compact freestanding ELF payloads without changing their virtual addresses.
This is a small executable-loading milestone, not a general program installer.

## Contract and implementation

The trusted shell command `runfile NAME` reads at most 2048 bytes, closes the file,
validates ELF64 and runs it in a private address space. It explicitly grants the
shell's namespace authority, just like `run`. Untrusted launchers instead use
PROCESS_SPAWN with explicit grants. No new disk/device acquisition syscall exists.

`tools/packelf.py` retains ET_EXEC PT_LOAD segments, rewrites file offsets and sets
p_align=1. It does not relocate code, compress bytes or remove memory protection.
The kernel copies segment contents into zeroed independent pages; load addresses
remain page aligned. It checks offset/address congruence when alignment exceeds
one, power-of-two alignment, overflow, bounds, entry permissions, overlap and W^X.
Dynamic linking, arbitrary Unix ELF layouts and in-place exec remain unsupported.

`tools/mkdisk.py --file NAME PATH` can seed up to eight files in a **new** dedicated
snapshot image. It validates names, uniqueness and 2048-byte bounds before creation.
An existing image is preserved unchanged, not updated. The normal unseeded image
format and two-bank recovery remain compatible.

## Working program

`user/disk.asm` is built independently of the kernel and packed to 318 bytes. It
creates `disk-note`, writes `from-disk`, closes the handle and exits with status 42.
Its bytes are not embedded into the kernel.

```bash
make demo-disk
make run-window DISK_IMAGE=disks/demo64.img
# Or use make run with the same DISK_IMAGE argument for serial-only output.
```

```text
x64> runfile hello
[exit] 42
x64> cat disk-note
from-disk
x64> save
```

For your own compatible freestanding program, use `user/linker.ld`, the documented
INT80 ABI, and the packer; install it into a new image with `--file`. These limits
exclude ordinary Linux executables and most compiled C programs at present.

## Testing and risks

```bash
make test-exec
```

The QEMU test executes the on-disk ELF, checks its actual file write, rejects a
W+X mutant, malformed text and a missing file, verifies unchanged free-frame
accounting, saves and boots again. Independent host snapshot decoding checks
stored bytes and the new bank sequence; repeated image creation must preserve
an existing image byte-for-byte. Existing ELF mutation sanitizer tests exercise
the shared parser. Tests use temporary images only.

The shell is a trusted debugging tool and grants broad namespace rights. Programs
must not be treated as sandboxed from that namespace when launched with runfile.
Persistence still requires explicit save, is bounded to eight files and has not
been qualified for real hardware power failure. Larger files, directories,
runtime installation, service isolation and executable signatures are future work.
