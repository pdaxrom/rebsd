# Anonymous objects and the swap pager

The first ReBSD VM pager keeps policy in `sys/vm` and uses the shared MIPS
`pmap` only to install, age, and remove translations.  It does not restore the
old fixed-window process swapper.

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

## Page selection

The anonymous descriptor pool is the pager's stable clock queue.  Resident
pages use the existing `VM_PAGE_ACTIVE` and `VM_PAGE_INACTIVE` states.  A scan
clears hardware/software reference bits on referenced pages, moves an
unreferenced active page to inactive, and may evict an unreferenced inactive
page.  Wired, busy, or pager-busy pages are skipped.

The page daemon runs in process 0, where swap I/O may sleep.  It starts below
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
clustering is attempted.  A new slot is committed only after a successful
write.  On write failure the resident page and its dirty state are retained,
the new slot is returned, the clock advances, and another page may be tried.
On read failure the temporary physical page is returned while the swap slot is
kept for a later retry.  Permanent I/O errors therefore lose no known-good
resident data, increment `vm.swap_failures`, and fail the requesting user
fault instead of panicking the kernel.  Swap exhaustion likewise leaves the
candidate resident and allows other processes to continue; an allocation that
cannot reclaim any page fails normally.

The cumulative object, fault, and pager counters are exported as BSD sysctl
nodes under `vm`: `objects`, `anon_pages`, `object_resident`,
`object_swapped`, `zero_faults`, `cow_faults`, `pageins`, `pageouts`, and
`swap_failures`.

## Current serialization rule

The supported kernels are single-CPU and non-preemptive in kernel mode.
Object/map mutation and swap-map allocation therefore run serialized by the
existing kernel execution model; interrupt handlers do not enter the pager.
Busy flags protect the page currently undergoing copy or I/O from a nested
reclaim scan.  An SMP or preemptible kernel must add explicit object, clock,
and swap-map locks before enabling concurrency.
