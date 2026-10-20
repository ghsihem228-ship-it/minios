# Full 64-bit OS roadmap

The active target is x86_64 in `starter/`. The repository-root i386 kernel is a
separate implementation; its features do not automatically exist here. Retain
both trees until any migration is deliberate and tested.

This is a working educational kernel, not a completed desktop or production OS.
The following work remains; none of these items is satisfied by boot PASS lines.
Each milestone needs implementation, negative tests and a reviewed ABI contract.

1. **Extend the process foundation.** Private CR3 roots, timer-preempted user
   execution, x87/XMM state, trusted launch/kill/reap and cleanup are implemented
   in [milestone 15](15-processes.md). Still needed: capability-based public process
   handles/syscalls, threads, blocking waits/sleep, startup arguments, TLS, richer
   resource accounting and kernel-thread integration.
2. **Service IPC and waits.** Blocking channels/wait sets, endpoint closure,
   backpressure, handle transfer with rights attenuation, cancellation, quotas
   and process-local authority. Typed message validation already exists but is
   not a complete user-service transport.
3. **Hybrid services and drivers.** Move filesystem policy, networking and drivers
   behind capability-scoped services. Establish device/MMIO/IRQ grants and DMA
   isolation policy. Current RAMFS, ATA and console live in the kernel.
4. **Storage and executable loading.** Scalable filesystem and directories,
   disk-backed ELF execution, reliable allocation/recovery, richer failure
   injection and runtime program installation. Current whole-namespace snapshots
   only hold eight 2 KiB files; only embedded ELF programs can run.
5. **Network stack.** Device discovery, a tested NIC driver/service, Ethernet,
   ARP/IPv4, UDP/TCP, DNS, bounded retransmission and capability-owned sockets.
   No network device is attached by the x64 default launch targets today.
6. **Developer environment and POSIX.** Explicit process startup arguments and
   environment, runtime/library coverage, process/file semantics, pipes with
   EOF/blocking, toolchain port and compatibility testing. The present source
   subset does not run existing Unix binaries.
7. **Transactions and installation.** Process-family snapshots, coordination with
   services, persistent install staging, conflict/resource accounting and a clear
   policy for irreversible effects. Existing COW undo covers a bounded single
   runner and existing-file writes, not arbitrary process trees or network effects.
8. **User experience and deployment.** UEFI/real-hardware boot, framebuffer/input,
   service supervisor, launch broker, consent/identity, installer/update recovery,
   packaging and diagnostics. Native QEMU launching is an emulator convenience.
9. **Hardening and ABI release.** Concurrency audits, fuzz campaigns, security
   regression tests, entropy/time sources, SMP, fault containment, resource limits,
   compatibility negotiation and long-term ABI tests. The current small ABI is
   documented; do not manufacture placeholder syscalls to reach a call count.

Near-term priority: capability-based process handles and user-service IPC with
wait/closure semantics. These support the requested hybrid architecture.
