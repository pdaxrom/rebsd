# Legacy VM Assumption Audit

This audit is the migration checklist for the fixed-window implementation.
It records units and ownership before any call site is converted to the new
4 KiB VM page geometry.

Phase 4 removed the fixed `u0`, whole-process swapping, and the shared wired
user mapping from the running process model.  The tables below deliberately
retain the pre-VM state as an audit record.  Board physical maps still reserve
the legacy user-window ranges until the later pager and rollout phases can
return them safely to the page allocator.

## Legacy size and page-like units

`NBPG` is 1024 on all current MIPS ports.  It is a historical accounting
"click", not the hardware TLB page and not the new VM page.  `PGOFSET`,
`CLBYTES`, `btoc`, and `ctob` inherit that unit.

The complete in-kernel use inventory is:

| Symbol or field | Files | Classification and migration rule |
| --- | --- | --- |
| `NBPG`, `PGOFSET`, `CLBYTES`, `btoc`, `ctob` definitions | `sys/mips/machparam.h`, `sys/mips/ci20/machparam.h`, `sys/mips/n64/machparam.h` | Legacy 1 KiB accounting and mbuf-cluster geometry.  Do not redefine these when introducing 4 KiB VM pages. |
| `btoc`, `ctob` | `sys/kernel/uipc_mbuf.c` | Rounds the old contiguous network DMA area in 1 KiB clicks.  Convert only with the DMA allocator, not as a mechanical VM-page substitution. |
| `memaddr` type and `miobase`/`miostart`/local `base` | `sys/include/types.h`, `sys/kernel/uipc_mbuf.c` | Legacy core/DMA click address.  It is unrelated to the local variables named `memaddr` in board `devsw.c`. |
| local `uintptr_t memaddr` | Malta, Ci20, and N64 `devsw.c` | Byte address used by `/dev/mem`; already a pointer-width integer, not the legacy `memaddr` typedef. |
| `p_dsize`, `p_ssize` | `init_main.c`, `kern_mman.c`, `exec_subr.c`, `kern_exit.c`, `kern_fork.c`, `vm_sched.c`, `vm_swap.c`, MIPS `signal.c` and `exception.c` | Despite stale "clicks" comments in `proc.h`, all current assignments, bounds, copies, statistics, and swap I/O treat these fields as byte counts.  Keep them bytes until replaced by `vm_map` ranges. |
| `p_daddr`, `p_saddr`, `p_addr` | exec, fork, swap, scheduler, signal, and exception paths | Overloaded: byte virtual addresses while resident; swap-block addresses while swapped.  Do not reuse these fields as typed VM addresses without first removing that state-dependent meaning. |

`btod`/`dtob` are swap-device block conversions and remain distinct from both
the 1 KiB click and the 4 KiB VM page.  The Phase 1 code therefore introduces
`VM_PAGE_SIZE` without changing any legacy macro.

## Fixed-address dependencies

The following paths depended on the single wired user window and were moved
together in Phase 4 rather than converted independently:

| Area | Current dependency |
| --- | --- |
| startup/linker | Board linker scripts place `u0` and `u` at fixed KSEG0 addresses outside normal kernel BSS.  `startup()` installs global wired user and framebuffer TLB entries. |
| scheduler/swap | Only one process image is resident in the shared physical user window.  `swapin`, `swapout`, `newproc`, and scheduler state overload process address fields with virtual or swap addresses. |
| `exec` | Loaders require data at `USER_DATA_START`, stack ending at `USER_DATA_END`, and reject images outside the fixed window.  ELF and a.out both rely on these checks. |
| `brk` | `kern_mman.c` grows data in place, clears bytes directly, and checks the fixed `MAXMEM` budget. |
| stack/signals | MIPS exception and signal code grow `p_ssize` downward from `USER_DATA_END`; signal frames are direct writes into the fixed mapping. |
| fork/vfork | `kern_fork.c` copies or swaps the fixed image and transfers byte-sized data/stack accounting between parent and child. |
| user access | `copyin`, `copyout`, `baduaddr`, `fuword`/`suword`, ptrace, and `/dev/mem` validate against one fixed user interval and assume no demand faults. |
| core dumps | Core/resource accounting consumes the same byte-sized data and stack ranges and assumes they are immediately addressable while resident. |
| reboot on N64 | Resident stage0 is at physical `0x00300000`; it aliases the 8 MiB user window and, on a 4 MiB system, part of the framebuffer reserve. |

The legacy mode was the only runtime mode during Phases 1 and 2.  Phase 4
switched process execution to per-process pmaps while retaining both ELF and
a.out execution.

## Repeatable searches

Run these from the repository root after changes to keep the audit current:

```
rg -n '\b(NBPG|PGOFSET|btoc|ctob|memaddr|p_dsize|p_ssize)\b' sys --glob '*.[chS]'
rg -n '\b(u0?|USER_DATA_(START|END)|MAXMEM|USIZE|p_(addr|daddr|saddr))\b' sys --glob '*.[chS]'
rg -n '\b(copyin|copyout|fuword|suword|ptrace|core|longjmp|swapin|swapout)\b' sys --glob '*.[chS]'
```
