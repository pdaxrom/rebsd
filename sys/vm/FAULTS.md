# VM fault contexts

Every demand fault carries an explicit context.  The low four bits select
`VM_FAULT_USER`, `VM_FAULT_COPY`, `VM_FAULT_KERNEL`, or
`VM_FAULT_INTERRUPT`; `VM_FAULT_CAN_SLEEP` is separate.

- A user TLB miss first tries the resident `pmap`.  A slow object fault
  temporarily enables interrupts and uses `VM_FAULT_USER | VM_FAULT_CAN_SLEEP`
  so vnode and swap I/O may sleep legally.
- `copyin` and `copyout` use `VM_FAULT_COPY`.  They add
  `VM_FAULT_CAN_SLEEP` only outside interrupt context and only while
  interrupts are enabled.  Otherwise they can use resident pages but return
  `EWOULDBLOCK` before allocation, COW, pagein, pageout, or busy-page waiting.
- Interrupt handlers never initiate pager I/O.
- Kernel-address TLB exceptions do not consult a user `vm_map` and never
  allocate.  They refill an already published kernel `pmap` translation or
  panic as a kernel mapping bug.

Direct `vmspace_read` and `vmspace_write` are process-context helpers used by
exec, initialization, and vnode integration.  Callers which cannot sleep must
use their `_context` forms without `VM_FAULT_CAN_SLEEP`.

Diagnostic invariants are enabled reproducibly with `VM_DIAGNOSTIC=1` on the
normal out-of-tree board build command.  They add panic-on-corruption checks
without changing the user ABI.  Production MIPS builds retain the saturating
fast-refill counter but omit the per-refill CP0/PTE/register snapshot;
`VM_DIAGNOSTIC=1` restores that complete snapshot.

The shared VM hot paths use one common next-fit physical-page allocator and a
hash for resident `(object, page-index)` lookup.  The shared MIPS `pmap` keeps
reverse mappings for managed pages, so page reclaim and reference/modify
clearing visit only mappings of the requested physical page.  Cached N64
mappings retain the existing four-colour invariant; range cache maintenance
therefore operates on the matching KSEG0 indices instead of sweeping both
complete caches.

Normal demand faults and user protection failures do not print to the kernel
console.  Fatal kernel mapping faults still dump their context before panic.
`MIPS_TRACE=1` or `N64_TRACE=1` enables concise user-fault tracing, capped at
32 messages per boot so a faulty process cannot monopolize the console.
