# i686 whole-port reuse audit

Status: active cleanup after the 2026-07-26 audit.

This document is the inventory for the whole i686 port.  It covers every
kernel area touched during bring-up, not only disk, VFS, FAT, and file
descriptors.  A QEMU marker is test evidence; it does not by itself prove that
the implementation belongs in `sys/i386`.

## Baseline correction

The i686 branch was originally forked from stale commit `f8152ae5` instead of
the current `dev` branch.  Current `dev` contained eight commits absent from
the port.  In particular, the Ci20 `GPR64` userland-smoke failure was already
fixed on `dev` by `MIPS_VM_PROCESS_SMOKE_GPR64`.

Calling that failure an unrelated old defect was incorrect.  The port is now
being merged with current `dev`, and a full Ci20 GCC build is a required gate
for the cleanup commit.

## Reuse violations removed

| Removed i686/private code | Reused owner |
| --- | --- |
| `sys/i386/common/libkern.c` | `sys/kernel/libkern.c`, moved from the already shared MIPS implementation |
| MIPS-only location of `stubs.c` plus private i386 `nosys` ownership | `sys/kernel/stubs.c` |
| `sys/i386/common/sysent_bootstrap.c` | common `sys/kernel/init_sysent.c`, with a build-time prefix selection |
| `sys/i386/pc/process_lifecycle.c` | common `sys/kernel/kern_exit.c` and `sys/kernel/kern_resource.c` |
| `sys/kernel/kern_proc_lifecycle.c`, added by the i686 branch | the same existing common exit/wait/reap path |
| weak i386 `psignal`, `issignal`, `postsig`, `setpri`, `setrq`, and `swtch` fallbacks | common `kern_sig.c`, `kern_sig2.c`, `kern_synch.c`, and `kern_clock.c` |
| weak i386 `noproc` and `time` storage | common `kern_clock.c` and `kern_time.c` |

The zombie test was corrected to follow the existing common lifecycle:
`kern_exit.c` destroys a dead process's vmspace before it becomes waitable.
The former private lifecycle kept the vmspace alive and taught the QEMU test
the wrong contract.

## Audit of shared-kernel changes

The i686 commits changed common kernel and MIPS sources as well as adding
`sys/i386`.  Each class was compared with the pre-port implementation.

| Change | Result |
| --- | --- |
| Rename MIPS-specific process/u-area entry points to `md_*` | Keep.  This exposes the existing architecture contract without changing MIPS behavior. |
| Add `md_user_frame_*` operations used by exec and ptrace | Keep.  The common code no longer reads MIPS frame slots directly. |
| Split user `copyinstr` from kernel `copykstr` and carry the namei source-space flag | Keep.  User pointers and kernel shebang/path strings now have explicit, different paths. |
| Move `bioinit` ownership out of MIPS startup and into common buffer-cache initialization | Keep.  This is common subsystem initialization. |
| Factor descriptor close/release helpers used by exit/fork tests | Keep.  The implementation stays in `kern_descrip.c`; i386 has no copy. |
| Generalize VM/pmap comments and add explicit no-swap/single-thread bootstrap capabilities | Keep for the bring-up configuration.  The VM implementation remains common. |
| Add `kern_proc_lifecycle.c` and private i386 lifecycle helpers | Remove.  These duplicated existing exit/wait/resource code. |
| Add private i386 libkern, sysent, and weak signal/scheduler implementations | Remove or replace with common objects, as listed above. |

The call-site audit found no remaining use of ambiguous `copystr`.
Executable arguments, UFS namei, and mount paths use `copyinstr` for user
addresses; exec-script and kernel pathname substitutions use `copykstr`.

## i386 source inventory

### Required machine-dependent implementation

These files own hardware or ABI behavior that the common kernel cannot
provide:

- `boot/entry.S`, `boot/setup.S`, `boot/interrupt_entry.S`, and the linker
  scripts: Linux boot protocol, BIOS INT13 loader, A20, E820, protected-mode
  entry, and x86 interrupt frames.
- `common/context.S`, `common/privilege.S`, `pc/tss.c`, `pc/interrupt.c`,
  `pc/pic.c`, and `pc/pit.c`: i386 context restore, rings, TSS, IDT, 8259A,
  and 8254.
- `pc/memory.c`, `pc/paging.c`, and `common/pmap.c`: E820 normalization,
  non-PAE page tables, CR3, `invlpg`, and the i386 backend of the common pmap
  contract.  Pmap policy that can be shared later must not be copied from or
  into the MIPS backend casually.
- `common/copyio.c` and the MD part of `common/vm_machdep.c`: user-address
  access through the active i386 vmspace, u-area allocation, fork frames, and
  saved-register operations required by common process/exec/ptrace code.
- `common/signal.c`, `common/trap.c`, and `common/user_return.c`: i386 signal
  frame, exception-to-signal mapping, selectors/EFLAGS validation, and the
  return-to-CPL3 ABI.  Signal selection, delivery policy, priorities, and run
  queues come from the common kernel.
- `pc/syscall.c` up to the MD dispatcher: the `int 0x80` register ABI and
  carry/errno/restart conversion.  Syscall numbering and handlers come from
  common `init_sysent.c`.
- `pc/pci.c`, `pc/ide.c`, `pc/early_console.c`, and the MD portions of
  `pc/devsw.c`: PCI configuration mechanism 1, legacy ATA PIO, VGA/COM
  console, and device-switch adapters.  Partitioning, buffer I/O, VFS, FAT,
  and descriptor semantics remain common.

The matching headers under `sys/i386/include` are retained only when they
describe one of those hardware/ABI contracts or a temporary component listed
below.

### Common code already linked by i686

The current i686 image directly compiles the existing common disk layer,
UFS/FAT VFS, vnode/file-descriptor path, VM objects/maps/vmspace, fork,
exit/wait/resource, signal policy, scheduler, clock, syscall prefix, libkern,
and syscall stubs.  I386 adapters pass an ATA-backed `dev_t` into those
interfaces; there is no i386 FAT parser or private file table.

### Bring-up tests, not production subsystems

The following files are in-kernel QEMU tests.  They do not justify a parallel
kernel implementation and must eventually be excluded from the normal
hardware image:

- `pc/context_selftest.c`
- `pc/copyio_selftest.c`
- `pc/signal_selftest.c`
- `pc/trap_selftest.c`
- `pc/uarea_selftest.c`
- `pc/user_return_selftest.c`
- the self-test portions of `common/pmap.c`, `pc/paging.c`,
  `pc/privilege.c`, `pc/syscall.c`, `pc/vm_bootstrap.c`,
  `pc/vmspace_bootstrap.c`, and `pc/process_bootstrap.c`

Removal condition: introduce a QEMU diagnostic build option and make the
default kernel use the normal common startup path without executing destructive
bring-up probes.

## Common exec reuse completed

The former `common/elf_bootstrap.c` and `include/elf_bootstrap.h` have been
removed.  Memory-backed bootstrap images and inode-backed `exec` now share
the target ELF header validator in `sys/kernel/exec_elf_image.c`; the target
machine and byte order continue to come from each architecture's
`machine/elf_machdep.h`.

The common memory-image loader uses the existing VM address/size overflow
helpers, user-address limits, map-entry capacity, vmspace mappings and
protection operations.  It supports validated non-overlapping `PT_LOAD`
segments, BSS zeroing, final segment permissions, W^X rejection and complete
rollback without an i386-private executable loader.

The former `common/user_stack.c` and `include/user_stack.h` duplication has
also been removed.  The i386 bootstrap fills `struct exec_params` and calls
the common `sys/kernel/exec_subr.c::exec_setupstack`.

The historical inode-backed path in `sys/kernel/exec_elf.c` still accepts only
its original single RWX `PT_LOAD` layout through `exec_estab`.  That is now
recorded as a limitation of the common exec implementation, not worked around
by architecture-private code.  Connecting normal i386 `exec`/`init_main`
requires extending that owning common path while preserving existing MIPS
behavior.

## Remaining reuse violations

These pre-existing parts still work, but violate the project reuse and
no-workaround gates.  They block completion until replaced by their owning
common kernel paths.

1. `common/initfs.c`, `tools/mkinitfs.py`, and the embedded initfs image are a
   private file container used only as a no-disk fallback.

   Removal requires using an existing block-device filesystem path.  The
   preferred target is the same read-only UFS/romdisk model used by
   Ci20/Malta, with only an i386 memory-backed device adapter.  The IDE-CF
   path must continue to mount through the common disk/VFS/filesystem stack.

2. `pc/process_bootstrap.c`, `pc/vm_bootstrap.c`,
   `pc/vmspace_bootstrap.c`, and much of `pc/boot_main.c` manually establish
   proc0/proc1 and startup state also owned by `sys/kernel/init_main.c`.

   Removal requires linking the common startup and exec path after the
   console/root-device adapters exist.  Process lifecycle itself is already
   common; no additional i386 process policy may be added here.

3. `pc/scheduler.c` contains genuine MD `idle()`, but its early
   console-only `panic`/`log` fallback overlaps `sys/kernel/subr_prf.c`.

   Removal requires a small i386 `cnputc` console adapter and the appropriate
   common printf/tty dependencies.  Until then the fallback remains weak and
   is classified as bootstrap-only, not as an i386 logging subsystem.

4. `common/vm_machdep.c` contains a weak fail-stop `md_init_process` because
   the current diagnostic image does not yet link common `init_main.c`.

   It must disappear when item 2 is complete.  No process creation logic may
   be added to this fallback.

## Validation gates

Every cleanup commit must pass:

- i686 GCC full rebuild;
- QEMU Linux-protocol and BIOS boot;
- FAT16 and FAT32 roots;
- IDE absent and `/sbin/init` absent fallback paths while they exist;
- divide, general-protection, and page-fault negative gates;
- host FAT, disk, and VM tests;
- a current `BOARD=ci20` GCC build after any common-kernel change.

Real IBM 6563-W4G testing is not required until these QEMU and cross-platform
gates are green and the next hardware image is explicitly requested.
