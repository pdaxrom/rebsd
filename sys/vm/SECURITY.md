# VM security invariants

The current MIPS kernels and user programs use a 32-bit ABI.  Every
`vmspace` covers `0x00001000` through `0x7fffffff`; the shared MIPS `pmap`
rejects user translations at or above `0x80000000`.  Kernel mode is entered
through the MIPS KSU state and kernel faults never create mappings which are
absent from the active process's user `vm_map`.

All address ranges use a representable exclusive end.  Public VM operations
reject zero-sized, wrapped, unaligned, or user/kernel-crossing ranges before
changing either `vm_map` or `pmap`.  VM-object offsets are 64-bit, but each
offset plus its 32-bit range length is checked independently.  Executable
headers are checked before their addresses and sizes are used to construct a
new address space.

## Write and execute policy

`mmap` and `mprotect` reject a protection containing both write and execute
with `EACCES`.  Character-device and shared-memory mappings are never
executable.  This prevents creation of new W+X mappings through the public VM
ABI.

The supported MIPS32r2 and VR4300 TLB formats do not provide a usable
per-page execute-disable bit, so read/write mappings cannot be made
hardware-NX.  In addition, the preserved OMAGIC and current toolchain ELF ABI
place code and writable data in one historical `PF_R|PF_W|PF_X` load segment.
That initial executable image is the sole compatibility exception to the
public W^X policy.  Removing it requires a separately coordinated executable
format and linker-script transition; it is not silently treated as protection
that the hardware can enforce.

## Physical-page disclosure

The kernel bootstrap allocator installs a direct-map page callback before any
runtime VM consumer starts.  Allocation first verifies the diagnostic free
poison and then scrubs every byte of every selected page to zero.  A scrub
failure aborts the allocation and restores the free poison.  Consequently a
page previously owned by another process, a page table, or a kernel user area
cannot be mapped into a new process with stale contents.

Anonymous demand faults zero the page again, swap faults replace it with the
saved page, file faults load it through the vnode pager, and COW faults replace
it with the source page's contents.  These second-layer operations define the
new mapping's contents; the allocator scrub is the non-bypassable disclosure
boundary.
