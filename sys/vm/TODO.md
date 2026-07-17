# ReBSD Virtual Memory TODO

This document is the implementation roadmap for replacing the current fixed
MIPS user-memory window with a real virtual-memory subsystem.  It also covers
`mmap`, shared memory, copy-on-write `fork`, and the related machine-dependent
MMU work.

No VM implementation is enabled by this file.  Until the milestones below are
implemented and tested, all supported boards must retain their current memory
layout and process behavior.

## Goals

- Keep policy and data structures in machine-independent `sys/vm` code.
- Keep TLB, ASID, cache, and page-table operations in shared MIPS code.
- Keep board code limited to describing RAM and reserved physical ranges.
- Use a 4 KiB VM page independently of the legacy `NBPG == 1024` accounting
  unit.  Do not change `NBPG` globally as part of the initial VM work.
- Give every process an independent address space, kernel stack, and user area.
- Support anonymous and file-backed mappings, copy-on-write, swapping, and
  shared memory without adding `/proc` or Linux-specific interfaces.
- Preserve the existing ELF and a.out ABIs and existing non-VM system calls
  unless a separately reviewed ABI change is required.
- Keep the initial VM implementation on the current 32-bit kernel and user
  ABIs on every supported board, including Malta64 and N64.
- Keep MMIO and bus addresses distinct from allocatable RAM even when both fit
  in the current 32-bit physical-address type.
- Use 64-bit types only where the current ABI already needs them, such as file
  and VM-object offsets; do not let them implicitly widen addresses or sizes.

## Non-goals for the first implementation

- Demand-paged kernel memory.
- Kernel modules or loadable VM components.
- NUMA, huge pages, memory overcommit policy, or transparent page merging.
- Linux `/proc`, `/dev/shm`, `memfd`, or Linux-only `mmap` flags.
- A 64-bit kernel or 64-bit user ABI.  The 64-bit-capable CPUs used by Malta64
  and N64 continue to run the current 32-bit kernel and user ABIs.
- Changing the on-disk UFS format merely to introduce VM.  A future large-UFS
  format remains a separate filesystem project.

## Current constraints and baseline

The recorded layouts and TLB state are in [BASELINE.md](BASELINE.md).  The
legacy units and fixed-address call sites are classified in
[LEGACY_AUDIT.md](LEGACY_AUDIT.md).

- [x] Record a repeatable baseline for Ci20, Malta, Malta64, and N64 before
  changing memory behavior.
- [x] Document each board's RAM size and every reserved physical range:
  kernel image, exception vectors, fixed `u`/`u0`, legacy user window, rootfs,
  `/var` RAM disk, swap, DMA pool, framebuffer, and device memory.
- [x] Document the existing wired MIPS TLB entries and virtual/physical user
  window on each board.
- [x] Audit every use of `NBPG`, `PGOFSET`, `btoc`, `ctob`, `memaddr`, and
  process `p_dsize`/`p_ssize`; classify it as accounting, swap, hardware page,
  or VM page usage before changing it.
- [x] Audit fixed-address assumptions involving `u`, `u0`, process stacks,
  `exec`, `fork`, `sbrk`, `longjmp`, core dumps, ptrace, and swap.
- [ ] Preserve a build-time legacy-VM mode until the replacement passes the
  complete build and runtime matrix.

## Phase 1: VM geometry and physical memory map

- [x] Define fixed-width 32-bit VM types for virtual addresses, physical
  addresses, page numbers, protection bits, and sizes on all current targets.
- [x] Define a 4 KiB VM page size and checked page alignment/conversion helpers.
- [x] Add compile-time checks for the 32-bit kernel/user ABI, address width,
  overflow, and alignment assumptions on MIPS32 and the current
  Malta64/VR4300 and N64/VR4300 configurations.
- [x] Implement a bootstrap physical-region map with no dynamic allocation.
- [x] Let board code register RAM and named reserved regions without exposing
  board constants to machine-independent VM code.
- [x] Validate that regions are aligned, ordered, non-overlapping, within RAM,
  and representable by the selected physical-address type.
- [x] Print a concise boot summary only while the new allocator is diagnostic;
  remove or gate verbose output before normal enablement.
- [x] Add host tests for region insertion, splitting, coalescing, reservation,
  clipping, alignment, overflow, and malformed maps.

Exit criteria:

- All current kernels build and boot with unchanged process behavior.
- The new physical map exactly accounts for usable and reserved RAM on each
  board, but does not yet allocate pages for running processes.

## Phase 2: Physical-page allocator

- [x] Add `vm_page` metadata with explicit states: free, wired, active,
  inactive, cached, busy, laundry, and bad/reserved.
- [x] Implement allocation/free for single pages and aligned contiguous runs.
- [x] Track wire, hold, reference, dirty, and busy counts without silently
  wrapping them.
- [x] Add allocation constraints needed by DMA and devices, including maximum
  physical address, alignment, boundary, and contiguous length.
- [x] Keep cacheability as a mapping attribute; do not create two independent
  owners for cached and uncached aliases of one physical page.
- [x] Add deterministic low-memory and fragmentation tests.
- [x] Add invariants and diagnostic poisoning for double-free, use-after-free,
  overlapping allocation, and freeing reserved pages.
- [x] Export stable VM counters through BSD-style `sysctl` nodes.

Exit criteria:

- The allocator passes host tests and a kernel self-test using only pages not
  owned by the legacy runtime.
- No existing DMA, rootfs, swap, or RAM-disk range can be allocated.

## Phase 3: MIPS pmap, TLB, and ASIDs

- [x] Define a machine-independent `pmap` contract and a shared MIPS
  implementation for map, remove, protect, extract, reference, and modify.
- [x] Implement MIPS page tables for 4 KiB pages without relying on the current
  wired 1 MiB user mappings.
- [x] Implement TLB refill, invalid, modified, and address-error paths with
  correct user/kernel fault separation.
- [x] Allocate and recycle ASIDs with generation tracking and required TLB
  invalidation on wrap.
- [x] Preserve required global/wired kernel mappings and define a documented
  wired-entry budget.
- [x] Implement targeted local invalidation before falling back to a full TLB
  flush.
- [x] Handle MIPS cache coherency for executable pages, data writes, DMA, and
  cached/uncached aliases.
- [x] Keep CPU-family differences behind shared MIPS operations: MIPS32r2 on
  Ci20/Malta and VR4300 behavior on Malta64/N64.
- [x] Add counters and optional diagnostics for refills, faults, ASID rollover,
  modified exceptions, and full/targeted shootdowns.
- [ ] Use JTAG on Ci20 to capture PC, SP, RA, Cause, EPC, BadVAddr, EntryHi,
  EntryLo, Context, and Status for MMU hangs that do not reach the console.

Exit criteria:

- Malta can create, access, protect, and remove test mappings under QEMU.
- Stale mappings are not visible after ASID reuse.
- Executable mappings work after code is copied or loaded into a fresh page.

## Phase 4: Per-process address spaces

- [x] Add `vmspace` and `vm_map` ownership to every process.
- [x] Replace fixed global `u`/`u0` process state with per-process user areas
  and kernel stacks.
- [x] Switch address spaces and kernel stacks in the scheduler without copying
  an entire fixed user area.
- [x] Convert `exec` to construct a new address space and commit it atomically.
- [x] Convert `sbrk`/data growth and user-stack growth to VM map operations.
- [x] Add guarded kernel stacks and a user-stack guard region where hardware
  and address space permit.
- [x] Make `copyin`, `copyout`, `fuword`, `suword`, signal delivery, ptrace,
  and core dumping safe across page boundaries and faultable mappings.
- [x] Tear down all mappings and references reliably on `exit` and failed
  `exec`.
- [x] Define fork/exec/exit locking rules before enabling concurrent VM paths;
  see [LOCKING.md](LOCKING.md).

Exit criteria:

- Multiple processes have genuinely different virtual mappings at the same
  virtual address.
- Existing userland, signals, pipes, sockets, compilers, and filesystem tests
  pass with no fixed-window fallback.

## Phase 5: Anonymous memory, copy-on-write, and paging

- [ ] Add VM objects and anonymous-page descriptors independent of processes.
- [ ] Implement demand-zero anonymous faults.
- [ ] Implement `fork` using copy-on-write mappings and correct write-fault
  promotion.
- [ ] Preserve sharing for mappings explicitly marked shared.
- [ ] Add page queues and a page daemon with documented free-page targets.
- [ ] Add a swap pager using the existing swap device interface while keeping
  swap block units distinct from VM page units.
- [ ] Allocate and free swap slots safely, including error rollback.
- [ ] Define dirty-page writeback, clustering, retry, and permanent-I/O-error
  behavior.
- [ ] Ensure process termination cannot leak VM pages or swap slots.
- [ ] Add memory-pressure, fork storm, COW isolation, swap exhaustion, and
  forced-I/O-error tests.

Exit criteria:

- A child initially shares its parent's anonymous pages, separates them on
  write, and returns all resources on exit.
- The system remains responsive and recoverable under controlled memory and
  swap exhaustion.

## Phase 6: `mmap` and mapped files

- [ ] Define BSD-compatible public constants and structures for `mmap`,
  `munmap`, `mprotect`, `msync`, `madvise`, `mincore`, and `mlock`/`munlock`.
- [ ] Add syscall-table entries, libc wrappers, manual pages, and native PCC
  header coverage.
- [ ] Extend syscall argument handling where 64-bit offsets or more than the
  current register argument set require stack arguments.
- [ ] Validate address, length, alignment, offset, protection, descriptor, and
  overflow combinations before changing a map.
- [ ] Support anonymous private mappings.
- [ ] Support file-backed `MAP_PRIVATE` with copy-on-write.
- [ ] Support file-backed `MAP_SHARED` with coherent reads, writes, truncation,
  `fsync`, `msync`, unmount, and device-removal behavior.
- [ ] Define vnode/page-cache ownership so buffered I/O and mapped I/O do not
  maintain incoherent independent copies.
- [ ] Implement partial unmap, map splitting/merging, protection changes, and
  faults at EOF.
- [ ] Keep filesystem-specific code behind vnode/pager operations; VM must not
  be tied to FAT, UFS, USB mass storage, or any block-device driver.
- [ ] Audit FAT and UFS locking and lifetime rules before enabling writable
  shared mappings.
- [ ] Add `/dev/zero` anonymous mapping and a controlled character-device
  mapping interface for framebuffer or device memory where appropriate.

Exit criteria:

- Anonymous, private file, and shared file mappings pass cross-process tests.
- `read`/`write`, mapping, `fsync`/`msync`, truncate, unmount, and hot-unplug
  observe one coherent state and return errors without deadlock or panic.

## Phase 7: Shared memory

- [ ] Implement POSIX shared-memory objects with BSD namespace/lifetime
  semantics and without requiring `/dev/shm`.
- [ ] Add `shm_open` and `shm_unlink` libc interfaces plus `ftruncate` and
  `mmap` integration.
- [ ] Implement SysV shared memory: `shmget`, `shmat`, `shmdt`, and `shmctl`.
- [ ] Define permissions, ownership, identifier reuse, deletion-after-last-
  detach, fork inheritance, and exec/exit detach behavior.
- [ ] Reuse VM objects for anonymous shared mappings, POSIX SHM, and SysV SHM;
  do not create separate physical-memory allocators.
- [ ] Add limits and read-only `sysctl` accounting for objects, pages, and
  per-process mappings.
- [ ] Add multi-process coherence, permission, lifecycle, exhaustion, and
  crash-cleanup tests.

Exit criteria:

- Parent and unrelated processes can share data with the documented POSIX and
  SysV interfaces, and all pages are reclaimed after the final reference.

## Phase 8: Reliability, security, and observability

- [ ] Enforce user/kernel address separation and W^X where supported.
- [ ] Never map freed physical pages into another process before clearing
  user-visible contents.
- [ ] Harden all range arithmetic against wraparound and signed truncation.
- [ ] Make faults from interrupt context, copy routines, and kernel mappings
  explicit; never sleep where sleeping is illegal.
- [ ] Define lock ordering for maps, objects, pages, vnodes, buffer cache, swap,
  and process teardown.
- [ ] Add useful `vmstat`/`pstat` output and BSD-style VM `sysctl` nodes.
- [ ] Keep normal boot and fault logs concise; gate high-frequency diagnostics.
- [ ] Add kernel assertions that can be enabled without changing the ABI.
- [ ] Document OOM selection/termination policy rather than hanging forever.

## Build and test matrix

Host tests must run before any board image is produced.  Each completed phase
must then pass the applicable matrix below.

- [x] Host unit tests with the host compiler and sanitizers where possible.
- [x] Malta MIPS32r2 hard-float: kernel GCC + userland GCC.
- [x] Malta MIPS32r2 hard-float: kernel PCC + userland PCC.
- [ ] Malta MIPS32r2 soft-float: kernel GCC + userland GCC.
- [ ] Malta MIPS32r2 soft-float: kernel PCC + userland PCC.
- [ ] Malta64/VR4300 hard-float: kernel GCC + userland GCC.
- [ ] Malta64/VR4300 hard-float: kernel PCC + userland PCC.
- [ ] Malta64/VR4300 soft-float: kernel GCC + userland GCC.
- [ ] Malta64/VR4300 soft-float: kernel PCC + userland PCC.
- [ ] N64 hard-float: kernel GCC + userland PCC, followed by hardware smoke.
- [ ] Ci20 MIPS32r2 hard-float: kernel GCC + userland GCC, followed by hardware
  smoke and JTAG-assisted hang capture when needed.

Required runtime coverage grows with the implementation but must include:

- [x] Boot, init, shell, signals, fork/exec/wait, and repeated process exit.
- [ ] Native PCC/GCC compiler and ABI smoke tests.
- [ ] Filesystem, networking, USB, and block-I/O regressions.
- [ ] Anonymous mapping and protection-fault tests.
- [ ] COW and shared-memory multi-process tests.
- [ ] File mapping, truncate, sync, unmount, and removable-media error tests.
- [ ] Low-memory, swap-pressure, resource-exhaustion, and fault-injection tests.
- [ ] Long-running fork/exec/mmap/shm stress with leak counters checked before
  and after the run.

## Rollout and commit policy

- [ ] Keep each milestone bisectable and preserve the legacy path until the
  replacement phase is proven.
- [ ] Do not combine generic VM, MIPS pmap, board layout, syscall ABI, and
  filesystem coherency changes in one commit.
- [ ] Do not commit hardware-dependent behavior as working until the relevant
  Ci20 or N64 test has been confirmed on hardware.
- [ ] Update this checklist and the affected manual pages with every confirmed
  milestone.
- [ ] Remove the legacy fixed-window VM only after every supported build and
  runtime configuration passes and there is a documented recovery path.

Suggested commit boundaries after confirmation:

1. VM geometry, types, physical map, and host tests.
2. Physical-page allocator and diagnostics.
3. Shared MIPS pmap/TLB/ASID layer.
4. Per-process vmspace and scheduler/exec conversion.
5. Anonymous pager, COW fork, and swap pager.
6. `mmap` ABI and anonymous/private mappings.
7. Coherent shared file mappings and device mappings.
8. POSIX and SysV shared memory.
9. Stress tests, observability, documentation, and legacy-path removal.
