# Experimental i686/BIOS port

This directory contains the GCC-only bring-up port for legacy BIOS PCs.  The
first hardware target is an IBM PC 300GL 6563-W4G with a Pentium III, VIA
Apollo Pro 133 chipset, a 3Dfx Voodoo3 AGP adapter, and legacy IDE-CF.

The initial image implements Linux/x86 boot protocol 2.02 and also contains
a native legacy-BIOS boot sector.  `rebsd-i686.bzimg` is loaded directly by
QEMU and by the IBM's existing GRUB Legacy, while
`rebsd-i686-bios-floppy.img` boots the same payload through BIOS INT 13h
without installing another boot loader.

## Build and smoke test

Use a separate object root:

```sh
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc all
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc bios-image-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc bios-boot-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc rootfs-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc ide-absent-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc bios-ide-absent-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc trap-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke-matrix
```

The normal smoke validates E820 normalization, generic `vm_phys`/`vm_page`
bootstrap, physical page allocation, bootstrap paging with `CR0.WP`, IDT
entry/return, and ten PIT timer IRQs.
The trap smoke deliberately raises divide error (`#DE`), general protection
(`#GP`), and a write-protection page fault (`#PF`) and requires a diagnostic
panic.  The matrix boots 32, 64, 128, 256, 768, and 1024 MiB QEMU
configurations.
Build artifacts are written under `O/obj/sys/i386/`; the source tree remains
clean.

The deterministic 1.44 MB floppy image uses only legacy CHS reads: its boot
sector loads the remaining setup sectors below 640 KiB, setup reads the
protected-mode payload into a bounded low-memory staging area, and the
protected-mode trampoline copies it to 1 MiB.  QEMU requires
`boot-loader: bios-int13`; direct `-kernel` requires
`boot-loader: linux-protocol`.  The first IBM 6563-W4G procedure is in
`docs/I686_HARDWARE_GATE.md`.

The deterministic UFS image produced by the existing `tools/fsutil` is
embedded as the read-only romdisk at block major 0, minor 0, matching the
Ci20 root-device layout.  The byte-backed block operations are owned by the
common `sys/disk/romdisk` driver; i386, Ci20, and Malta supply only their
linker-defined image bounds.  UFS is mounted through `vfs_mountroot`,
`/sbin/init` is resolved with common `namei`, and common inode-backed
`execve` reads and maps its ELF segments directly from the inode.  There is
no i386 filesystem parser, rootfs format, whole-file executable buffer, or
private executable loader.

IDE and USB mass storage use the separate generic block-device major 2 and
the common `sdN` namespace.  The current ATA backend attaches first as
`sd0`; the embedded romdisk never consumes an `sdN` unit.  The mandatory USB
phase will reuse the existing USB core, hub, `umass` BOT/SCSI transport, and
`sys/disk` backend contract.  I386 will supply only PCI interrupt/DMA and
host-controller attachment; it will not gain a private USB, mass-storage, or
disk stack.  IDE and USB attachment order may change their `sdN` unit, so
neither is selected as root.  The IBM IDE-CF and its Red Hat partitions
remain outside the debug-root policy.

Proc0 is initialized only by common `kern_proc.c::proc0_bootstrap`.
Common `newproc` allocates proc1, its u-area/vmspace, process-list entries,
PID hash entry, and scheduler frame.  The i386 init trampoline calls common
`kernel/init_process.c`; standard `icode` then invokes production syscall 11
to execute `/sbin/init`.  I386 owns only its context and user-entry ABI, not
process creation or exec policy.

Once in CPL3, init opens its own `/sbin/init` through production `open(5)`,
reads the ELF magic through `read(3)`, changes the file offset through
`lseek(19)`, and releases the descriptor through `close(6)`.  It keeps the
descriptor across the first production `fork`; the child reads through the
shared file object, advances the common offset, and early `exit` drops the
inherited reference before the parent closes the last one.  The gate also
requires `EBADF` after a repeated close and `EROFS` for a write open.

Porting invariant: `sys/i386` contains only hardware and ABI glue.  Existing
common disk, buffer-cache, VFS, filesystem and descriptor implementations
must be reused rather than reimplemented locally.

The low-level paging backend also self-tests map/unmap/protect/extract,
supervisor/user permissions, resident translation replacement, and targeted
TLB invalidation through `invlpg`.  The normalized E820 ranges back the
generic physical-page allocator through the standard
`vm_phys_board_register` hook.  Common `vm_phys_bootstrap` owns metadata
reservation, map finalization, and allocator initialization; i386 owns only
the E820 ownership description, direct map, and poison operation.  Bootstrap
and metadata pages stay reserved, and the generic allocation/free/poison
self-test runs in QEMU.  A permanent kernel direct map covers the first
1 GiB of physical memory.  The public
i386 pmap self-test creates two address spaces, switches CR3, validates
isolation/protection/execution, and verifies that wired page-table pages are
reclaimed.  The image also links generic `vm_map`/`vm_object`/`vmspace`;
its self-test exercises anonymous faults, clone+COW and an actual recoverable
i386 page fault.  The generic-kernel integration audit is in
`docs/I386_MD_API.md`.  Public `copyin/copyout` transfer through the active
vmspace and direct map; the QEMU self-test covers a cross-page demand fault,
COW isolation, invalid user ranges, protection and page reclamation.  The
i386 process layer also allocates and reclaims guarded 16 KiB wired u-areas;
its assembly context-switch self-test preserves callee-saved registers,
EFLAGS and execution state while moving to a separate u-area kernel stack
and back.  Fork/init now construct schedulable kernel frames and the QEMU
self-test follows the fork trampoline through the common `iret` epilogue
while switching between two process vmspaces.  Flat user descriptors and a
32-bit TSS now support a tested ring-3 entry, user trap, u-area stack switch
and return.  A DPL3 `int 0x80` gate now validates a six-register argument
ABI, two return registers and BSD-style Carry/errno results.  The i386
dispatcher now adapts an installed generic `struct sysent` table to
`u_arg/u_rval/u_error`, including `u_qsave`, `ERESTART`, and `EJUSTRETURN`;
the QEMU CPL3 stream exercises every return path.  Generic exec and ptrace now
use an opaque saved-user-frame API with tested MIPS and i386 backends.  The
i386 signal ABI now builds cdecl handler frames on regular or alternate user
stacks and validates complete `sigreturn` contexts; QEMU checks restoration,
selector/EFLAGS hardening, and allocator reclamation.  A CPL3-only common
return loop now delivers pending signals and performs priority/reschedule
work before `iret`; QEMU executes a real handler, trampoline and `int 0x80`
sigreturn through that path.  User CPU exceptions now become BSD pending
signals while kernel faults remain fatal; QEMU recovers through handlers
from both a real `UD2` and a terminal unmapped page fault.  An explicit
`copyinstr/copykstr` API now keeps low kernel addresses distinct from user
pointers across pathname and exec code.  The early syscall prefix has been
replaced by the complete common 0-177 `sys/kernel/init_sysent.c` table and
its real common handlers; QEMU invokes generic `kern_prot.c:getpid` as
syscall 20 from CPL3 and requires `syscall-production: ok`.  After the
destructive self-tests, a persistent process 1 owns a guarded u-area and
vmspace, keeps its CR3 and `md_curuser` active, and
provides the live kernel stack selected by `TSS.esp0`.  It now occupies
generic `proc[1]`, `allproc` and the PID hash, with proc0 reserved as the
idle slot.  Common `kern_proc.c::proc0_bootstrap`, shared with normal
MIPS/N64/Ci20 startup, owns proc0 vmspace, u-area, rlimits, signal state and
process queues; i386 supplies its guarded u-area and scheduler-format
`u_qsave`.  After process 1 returns from CPL3 on its u-area stack, the
kernel performs two proc1-to-proc0-to-proc1 switches.  Both switches change
CR3, `md_curuser` and `TSS.esp0`; the second resumes proc0's saved idle
continuation.  These are now real generic `kern_synch.c` switches: process 1
is inserted with `setrq`, proc0 selects it from `qs`, and `swtch` resumes
its `u_rsave`.  The i386 `spl*` contract preserves IF and the idle hook uses
the race-free `sti; hlt` sequence.  `process-bootstrap: ok`,
`process-table: ok`, `proc0-context: ok` and `scheduler-switch: ok` validate
that state.  Generic `newproc` also clones process 1 into PID 2 with a
separate vmspace and u-area.  Proc0 starts the copied CPL3 trapframe through
`i386_fork_trampoline`; the child observes `eax=0`, traps back, and switches
through proc0 to its parent.  `process-fork: ok` validates this complete
round-trip.  The fork is issued as production syscall 2 through `int 0x80`;
the parent observes PID 2 and `syscall-fork: ok` validates both return paths.
Process 1 enters
CPL3 with an RX text mapping and an RW stack without VM execute permission,
requires production `getpid` to return 1, and requires `process-user: ok`
before timer IRQs.  The target non-PAE Pentium III has no hardware NX bit.
The user payload is a separately linked ELF32/i386 `ET_EXEC`, installed as
`/sbin/init` in a deterministic read-only UFS image and loaded from two
`PT_LOAD` segments.  The embedded memory disk exposes that image through the
common disk, buffer-cache, VFS, inode and namei paths.
The common inode ELF loader checks
bounds, alignment, target ABI, entry, user ranges, overlap and W+X, zero-fills
BSS, applies final permissions, and requires `elf32-user: ok`; there is no
i386-private executable loader.
The common exec stack supplies `argc` in EBX, `argv` in ECX and `envp` in
EDX.  The standard initial arguments are `{"init", "-", NULL}` with
`envp == NULL`; the CPL3 image validates pointer arrays, packed strings,
alignment, reserved slots and the historical top `argv` word.
`user-stack: ok` is required.

The default cross toolchain is:

```text
/Users/sash/Library/i686-toolchain/bin/i686-elf-
```

Override it with `I686_TOOLCHAIN=/path` or `I686_PREFIX=/path/prefix-`.
Override QEMU with `QEMU_I386=/path/qemu-system-i386`.

Current scope is deliberately small: real-mode setup, A20, BIOS E820, flat
protected mode, COM1, VGA text output, a 256-entry IDT, CPU exception
diagnostics, remapped dual 8259A PICs, PIT IRQ0 at 100 Hz, normalized
physical RAM, non-PAE 4 KiB bootstrap paging, and reusable low-level page
mapping primitives.  It links the machine-independent `vm_phys`/`vm_page`
allocator and implements the public pmap contract with per-process address
spaces, generic vmspace, anonymous memory, COW and safe copy I/O.  The
diagnostic image does not yet enter the normal swap-initializing startup;
the remaining `VM_PAGER_NO_SWAP` build flag is an audited blocker, not an
accepted port policy.  The neutral process MD contract and structural
i386 u-area operations exist; the kernel can save and restore
scheduler-compatible i386 contexts, switch u-area stacks and resume copied
fork frames through the common interrupt return path.  User selectors, TSS
and ring-3 trap/return are connected and tested.  The low-level `int 0x80`
contract and generic `sysent` adapter now install the complete common syscall
table, including the real generic `getpid`, exec, VM and sysctl handlers.
A persistent process 1 created by common `newproc` now keeps a real u-area,
vmspace, CR3 and TSS kernel stack active after self-tests.
Proc0 has a separate u-area/vmspace and a reusable saved idle context;
generic `setrq`/`swtch` and the `qs` run queue are connected and tested
twice.  Generic `newproc` creates PID 2 and runs its cloned trapframe in
CPL3 through production syscall 2; production child `exit`/parent `wait4`
and reap paths are connected and tested.
The first persistent user mapping executes production syscall 20 from CPL3
and validates generic VM text/stack permissions.  Common inode-backed exec
maps RX text and RW data+BSS directly from `/sbin/init` on the mounted UFS,
and the common exec stack path supplies `argc/argv/envp`.
A read-only legacy primary-master ATA PIO backend attaches through the
generic disk layer and exercises real whole-device block strategy reads.
The common UFS/VFS path mounts the embedded memory-backed UFS device and
supplies `/sbin/init` to process 1; ATA writes and DMA are intentionally
absent.  The root is used through ordinary read-only
namei/open/read/lseek/close operations.
Writable storage, generic non-inode fileops, complete userland, and PCC
remain outside the current image.
