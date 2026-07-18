# Anonymous and file objects, and the pager

The ReBSD VM pager keeps policy in `sys/vm` and uses the shared MIPS `pmap`
only to install, age, dirty-track, and remove translations.  It does not
restore the old fixed-window process swapper.

## Ownership and faults

Each anonymous `vm_map` entry owns a reference to a VM object.  An object is a
sparse set of page-index nodes; an untouched index has neither a `vm_page` nor
a swap slot.  Its first fault allocates and zeroes one 4 KiB page.

Private `fork` clones the object nodes but shares their anonymous-page
descriptors.  Both mappings are made read-only while retaining their requested
write protection in `vm_map`.  A write fault allocates and copies a page only
when its anonymous descriptor still has multiple references.  Explicit
`VM_MAP_SHARED` entries reference the same object instead and never receive
copy-on-write treatment.

Object references, anonymous-page references, physical pages, and swap slots
are released independently.  Unmapping first removes every `pmap` reference;
the final anonymous reference can then return both its resident page and its
swap slot.  A failed `fork` or map insertion drops every reference it acquired.

## Private mapped files

A VM object may carry a filesystem-independent pager callback and cookie.
The inode adapter in `vm_vnode.c` is the only layer that knows about inode
locking, `rdwri`, and inode references.  Creating an object acquires its own
inode reference, cloning it for a private `fork` acquires another one, and the
last object release drops it.  A mapping therefore remains valid after its
descriptor is closed.

The first fault reads one page through the callback.  A short final page is
zero-filled; a page beginning at or beyond the current EOF fails with `ENXIO`,
which the MIPS user-fault path reports as `SIGBUS`.  Successful pages become
ordinary anonymous descriptors, so private writes and `fork` use the same COW
and swap paths as anonymous memory and never write the inode.

## Shared mapped files

All `MAP_SHARED` mappings of one live inode reference one sparse VM object.
Map entries carry their file offset into that object, so overlapping mappings,
aliases, and inherited mappings after `fork` resolve to the same anonymous-page
descriptor.  The object holds the inode reference; closing the descriptor does
not invalidate a mapping.  A writable shared mapping requires both a writable
descriptor and filesystem permission.  The initial 32-bit implementation
accepts shared file ranges only below the 2 GiB user/kernel boundary.

The shared object is the coherent in-memory owner while a page is resident.
Mapped writes are detected through MIPS modified exceptions and the physical
page's dirty count.  `msync`, `fsync`, final unmap, and memory-pressure reclaim
write dirty pages through the inode pager before clearing their modified state.
`MS_INVALIDATE` removes every alias after successful writeback, and a later
fault reloads the page through the filesystem.  Clean shared pages are dropped
and refaulted rather than consuming swap slots.  This file-backed reclaim path
does not require a configured swap device.

Ordinary UFS reads first write back dirty mapped pages in the requested range;
ordinary UFS writes copy the buffer-cache result into every resident alias.
Filesystems which delegate `rwip`, including FAT and ROMFS, use the same generic
inode hooks: a read first writes back overlapping mapped pages, while a write
first writes them back and invalidates them before the filesystem operation.
The next mapped access therefore reloads the filesystem's result.  Synchronous
writeback selects `syncip` for UFS and the mounted filesystem's `vfs_sync`
operation for delegated filesystems; VM code contains no FAT, UFS, ROMFS, USB,
or block-driver policy.

Truncation writes pages which remain below the new EOF, zeroes the unused tail
of a surviving partial page, and invalidates every page wholly beyond EOF.
Access to an invalidated page beginning beyond EOF reports `SIGBUS`.  The VM
object's inode reference also makes the existing `iflush` check return `EBUSY`
for unmount or device removal while a mapping survives, instead of leaving a
pager cookie pointing at detached filesystem state.

## POSIX shared memory

`shm_open` names kernel-resident anonymous VM objects directly; it does not
depend on a mounted `/dev/shm` filesystem.  The initial fixed namespace holds
32 objects, and a name is one leading slash plus up to 30 non-slash bytes.
Descriptors carry normal owner, group, mode, and read/write access checks.
`ftruncate` changes the exact logical length while the VM object tracks its
page-rounded extent.  A fault on a complete page beyond the current extent is
reported as `SIGBUS`, and bytes exposed by growth are zero-filled.

Shared mappings reference the namespace object's VM object, so descriptor
aliases, unrelated openers, and mappings inherited across `fork` see one
coherent set of pages.  Private mappings clone its sparse page nodes and use
the ordinary anonymous COW path.  `shm_unlink` removes only the name: open
descriptors keep the namespace record alive, mappings keep independent object
references, and immediate reuse of the name creates a distinct object.  The
last descriptor and mapping return the ordinary VM pages and swap slots; POSIX
SHM has no physical-memory allocator of its own.

System V shared memory uses a separate key and identifier namespace over the
same anonymous VM objects.  Identifiers combine a 32-slot table index with a
sequence number, so destroying and reusing a slot cannot revive a stale ID.
Each process tracks up to eight attachments in its `vmspace`; `fork` duplicates
those records, while `shmdt`, `exec`, and exit remove them.  `IPC_RMID` hides
the key and ID immediately but delays the base object release until the last
attachment disappears.  A fork of an already removed attachment remains
valid, while a new `shmat` cannot find the removed ID.

Both APIs limit an individual object to 64 MiB.  Read-only `vm` sysctls expose
the combined object and logical-page counts, the calling process's mapping
count, configured limits, and System V segment and attachment totals as
`shm_objects`, `shm_pages`, `shm_mappings`, `shm_max_objects`,
`shm_max_pages`, `shm_max_mappings`, `sysv_segments`, and
`sysv_attachments`.

## Page selection

The anonymous descriptor pool is the pager's stable clock queue.  Resident
pages use the existing `VM_PAGE_ACTIVE` and `VM_PAGE_INACTIVE` states.  A scan
clears hardware/software reference bits on referenced pages, moves an
unreferenced active page to inactive, and may evict an unreferenced inactive
page.  Wired, busy, or pager-busy pages are skipped.

The page daemon runs in process 0, where pager I/O may sleep.  It starts below
8 free pages and aims for 16.  A fault that reaches physical exhaustion also
runs a bounded synchronous clock scan so progress does not depend on process 0
being scheduled first.  These deliberately small fixed targets suit the
current 4-8 MiB user-memory configurations; changing them is policy, not an
ABI change.

## Swap units and failure rules

The VM page remains 4096 bytes.  The existing swap resource map and `swap()`
interface count `DEV_BSIZE` units (currently 1024 bytes), so one VM page owns
exactly four contiguous swap-map units.  Slot zero is never allocated.  A slot
remains attached to its anonymous page across page-ins and clean re-evictions.

Writeback is synchronous and currently operates on one VM page at a time; no
clustering is attempted.  A new swap slot is committed only after a successful
anonymous-page write.  On swap or shared-file write failure the resident page
and its dirty state are retained, the clock advances, and another page may be
tried.  A newly allocated swap slot is returned after failure.  On read failure
the temporary physical page is returned while a swap slot or file backing is
kept for a later retry.  Permanent swap I/O errors therefore lose no known-good
resident data, increment `vm.swap_failures`, and fail the requesting user fault
instead of panicking the kernel.  Filesystem pager errors propagate without
marking the page clean.  Swap exhaustion likewise leaves the candidate
resident and allows other processes to continue; an allocation that cannot
reclaim any page fails normally.

The cumulative object, fault, and pager counters are exported as BSD sysctl
nodes under `vm`: `objects`, `anon_pages`, `object_resident`,
`object_swapped`, `zero_faults`, `cow_faults`, `pageins`, `pageouts`, and
`swap_failures`.

## Current serialization rule

The supported kernels are single-CPU and non-preemptive in kernel mode.
Object/map mutation and swap-map allocation therefore run serialized by the
existing kernel execution model; interrupt handlers do not enter the pager.
Busy flags protect the page currently undergoing copy or I/O from a nested
reclaim scan.  The inode adapter also suppresses its buffered-I/O coherence
hooks while it is itself paging the same object, which prevents recursive
writeback.  The order is VM range, object/page, inode, filesystem buffer I/O;
no filesystem callback retains a VM busy flag after it returns.  An SMP or
preemptible kernel must add explicit map, object, inode, clock, and swap-map
locks before enabling concurrency.

Host tests force a dirty shared page through pageout and refault under physical
memory pressure.  The Malta QEMU smoke covers overlapping aliases, visibility
across `fork`, buffered read/write coherence, `fsync`, `msync`,
`MS_INVALIDATE`, truncate-to-`SIGBUS`, descriptor close, and shared-page access
during swap pressure with both GCC- and PCC-built kernels and user programs.
