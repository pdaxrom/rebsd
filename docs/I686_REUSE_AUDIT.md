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
| `sys/i386/common/sysent_bootstrap.c` and `REBSD_SYSCALL_BOOTSTRAP` | complete common `sys/kernel/init_sysent.c` table, with no architecture selection |
| `sys/i386/pc/process_lifecycle.c` | common `sys/kernel/kern_exit.c` and `sys/kernel/kern_resource.c` |
| `sys/kernel/kern_proc_lifecycle.c`, added by the i686 branch | the same existing common exit/wait/reap path |
| weak i386 `psignal`, `issignal`, `postsig`, `setpri`, `setrq`, and `swtch` fallbacks | common `kern_sig.c`, `kern_sig2.c`, `kern_synch.c`, and `kern_clock.c` |
| weak i386 `noproc` and `time` storage | common `kern_clock.c` and `kern_time.c` |
| `sys/i386/common/initfs.c`, `include/initfs.h`, `tools/mkinitfs.py`, and the later `disk_memory_attach` root path | deterministic UFS from existing `tools/fsutil`, exposed at major 0 minor 0 by common `sys/disk/romdisk` and mounted by common VFS/UFS |
| local `i386_disk_biodone` selected with a compiler macro | common `ufs_bio.c::biodone`; the local completion skipped buffer-cache read-ahead release |
| compile-time `printf`/`log` renames plus quiet i386 adapters | common `subr_prf.c`, `tty.c`, `tty_subr.c`, and `cons.c`; i386 provides only COM1/VGA poll/getc/putc/winsize hooks |
| weak i386 `panic`, `panicstr`, and `log` definitions | common `subr_prf.c`; the MD halt operation remains in the i386 console/boot boundary |
| duplicate i386 proc0 vmspace, u-area, rlimit, signal, and process-queue initialization | common `kern_proc.c::proc0_bootstrap`, also used by `init_main.c` for MIPS/N64/Ci20 |
| manual i386 proc1 slot, PID hash, u-area/vmspace initialization, and kernel-side `execve` | common `newproc`, the common process-1 trampoline in `kernel/init_process.c`, standard `icode`, and production user-side `execv` |
| weak i386 `md_init_process` fail-stop fallback | direct linkage of common `init_process`; i386 supplies only its scheduler trampoline and `md_user_enter` ABI |
| `pc/vm_bootstrap.c` and its private bootstrap API | common `vm_phys_bootstrap`; i386 now supplies only `vm_phys_board_register`, direct-map, and poison MD operations in `pc/vm_phys_board.c` |
| `i386_pmap_bootstrap_init`, `i386_vmspace_bootstrap_init`, and the private pmap-bootstrap header | direct calls to common `pmap_system_init` and `vmspace_system_init`; i386 keeps only its pmap backend, active-vmspace adapters, and selftests |
| PIT IRQ diagnostic counter without the kernel clock owner | standard MD `clkstart` programs/unmasks the 8254, and IRQ0 dispatches to common `hardclock`; the counter remains observation only |
| MIPS-local `ct_ticks`, `pipedev`, and version generator ownership | common `kern_clock.c`, `sys_pipe.c`, and architecture-neutral `tools/build/gen-vers.py` |

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
| Generalize VM/pmap comments and no-swap/single-thread bootstrap | Remove `VM_SINGLE_THREADED` and the private no-swap build selection.  Every kernel uses the same `tsleep`/`wakeup` busy-page contract; `swap none` now generates `NODEV`, and common `init_main` simply skips swap initialization when configuration supplies no device. |
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
- `pc/memory.c`, `pc/paging.c`, `pc/vm_phys_board.c`, and
  `common/pmap.c`: E820 normalization, the `vm_phys_board_register` ownership
  description, non-PAE page tables, CR3, `invlpg`, and the i386 backend of
  the common pmap contract.  Physical-map finalization, metadata reservation,
  and page-allocator bootstrap remain common.
- `common/copyio.c` and the MD part of `common/vm_machdep.c`: user-address
  access through the active i386 vmspace, u-area allocation, fork frames, and
  saved-register operations required by common process/exec/ptrace code.
- `common/signal.c`, `common/trap.c`, and `common/user_return.c`: i386 signal
  frame, exception-to-signal mapping, selectors/EFLAGS validation, and the
  return-to-CPL3 ABI.  Signal selection, delivery policy, priorities, and run
  queues come from the common kernel.
- `common/syscall.c`: the `int 0x80` register ABI and
  carry/errno/restart conversion.  Syscall numbering and handlers come from
  common `init_sysent.c`.
- `pc/pci.c`, `pc/ide.c`, `pc/early_console.c`, and the MD portions of
  `pc/devsw.c`: PCI configuration mechanism 1, legacy ATA PIO, VGA/COM
  console, and device-switch adapters.  Disk layout, buffer I/O, VFS,
  filesystem, and descriptor semantics remain common.
- `pc/dma.c` and `pc/usb_pci.c`: i686 coherent DMA-pool, PCI BAR/MMIO and
  IRQ attachment to the common DMA/USB/EHCI/OHCI interfaces.  USB
  enumeration, hub policy, HID boot-key decoding, BOT/SCSI, mass-storage
  media operations, partition parsing, disk naming and I/O remain common.
- `pc/early_console.c`: only COM1/VGA poll/getc/putc/winsize primitives for
  common `sys/kernel/cons.c`.  Console cdev, TTY queues and input policy are
  common and the former MIPS/N64 copies have been removed.

The matching headers under `sys/i386/include` are retained only when they
describe one of those hardware/ABI contracts or a temporary component listed
below.

### Common code already linked by i686

The current i686 image directly compiles the existing common console and TTY
driver, disk layer,
DMA allocator, USB core/task/hub, EHCI/OHCI HCDs, `ukbd` and `umass`
BOT/SCSI driver,
UFS VFS, vnode/file-descriptor path, VM objects/maps/vmspace, fork,
exit/wait/resource, signal policy, scheduler, clock, the complete syscall
table and its production exec/VM/sysctl handlers, process-1 initialization,
libkern, and syscall stubs.  The embedded UFS and ATA whole-device are both
registered through the common disk interface.  Only the embedded read-only
UFS is selected as root; ATA and USB remain additional devices.  There
is no i386 filesystem parser, partition policy, private file table, rootfs
format, USB core, HID decoder, SCSI transport, mass-storage driver, or
process-creation policy.

### Obsolete bring-up implementation removed

The private process/VFS bootstrap, the old one-off `/sbin/init` ELF and
rootfs manifest, and the DPL3 context/copy/signal/trap/u-area return test
suite have been deleted rather than retained as an alternative kernel path.
The production image therefore has no private proc1 constructor, private
VFS mount path, diagnostic return vector, or marker-only init program.

Backend validation that belongs to the implementation it tests remains with
the corresponding pmap, paging, and vmspace code.  It does not replace
common process, exec, VFS, filesystem, or userland owners.

## Common exec reuse completed

The former `common/elf_bootstrap.c`, `include/elf_bootstrap.h`, and
`sys/kernel/exec_elf_image.c` have been removed.  The common
`sys/kernel/exec_elf_loader.c` now owns ELF32 validation and mapping policy,
while `sys/kernel/exec_elf.c` reads each segment directly from the executable
inode.  The target machine and byte order continue to come from each
architecture's `machine/elf_machdep.h`.

The common loader uses the existing VM address/size overflow helpers,
user-address limits, map-entry capacity, vmspace mappings and protection
operations.  It supports validated non-overlapping `PT_LOAD` segments, BSS
zeroing, final segment permissions and W^X rejection without an
i386-private executable loader or a whole-file bootstrap buffer.

The former `common/user_stack.c` and `include/user_stack.h` duplication has
also been removed.  The i386 bootstrap fills `struct exec_params` and calls
the common `sys/kernel/exec_subr.c::exec_setupstack`.

The historical single RWX `PT_LOAD` restriction in `sys/kernel/exec_elf.c`
has been removed at the common owner.  MIPS/N64 user linker scripts now emit
separate RX and RW segments and both GCC kernel-object builds pass.  I386
enters `/sbin/init` through standard `icode` and the same production `execv`
path.

## Normal startup boundary

The audited i386 path no longer allocates proc1, edits the PID hash, builds
proc1 VM state, or invokes `execve` in machine-dependent C.  Common
`newproc` creates proc1, `init_process` maps standard `icode`, and that user
bootstrap invokes production syscall 11 for `/sbin/init`.

`pc/boot_main.c` now saves the BIOS parameters, initializes only MD hardware,
and calls common `sys/kernel/init_main.c::main`.  The obsolete private VFS
and process bootstrap implementations and their diagnostic probes have been
removed.  Common startup initializes tables, mounts the configured romdisk
root, creates proc1 and enters `/sbin/init`.

The production QEMU gate continues through getty and login into `/bin/sh`.
This replaces the former marker-only `HALT` gate with observable userland
execution.

## Validation gates

Every cleanup commit must pass:

- i686 GCC full rebuild;
- QEMU Linux-protocol boot through login and shell;
- embedded read-only UFS root with IDE present and absent, while IDE remains
  an additional read-only common block device;
- host disk and VM tests;
- current `BOARD=ci20` and `BOARD=n64` GCC builds after common-kernel changes.

The native BIOS floppy loader moves 64 KiB CHS-loaded chunks to high memory
with BIOS `INT 15h/AH=87`; its deterministic image and full userland boot are
part of the QEMU gate.  Real IBM 6563-W4G testing is not required until the
next hardware image is explicitly requested.
