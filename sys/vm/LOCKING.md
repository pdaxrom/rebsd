# VM serialization and lifetime rules

The current kernels are uniprocessor.  They do not pretend to provide SMP
mutexes: short metadata publication uses single-owner execution and, where
needed, an interrupt-priority barrier.  No code may retain that barrier across
allocation, pager, vnode, buffer-cache, swap, or device I/O.

## Ordering

Operations which touch more than one layer use this order:

1. process publication or scheduler state;
2. `vmspace` and `vm_map` ownership;
3. VM object, object-page, and anonymous-page metadata;
4. `pmap`, ASID, and TLB state;
5. physical-page allocator state.

Vnode and buffer-cache operations are outside that chain.  A pager first
publishes `VM_ANON_BUSY` and, for pageout or cache replacement, removes the
page's user translations.  It may then enter vnode -> buffer cache -> device
or swap I/O without an interrupt barrier.  Completion updates page state,
clears busy, and wakes waiters.  Code must not call back from a vnode, buffer,
or device lock into map/object mutation.

`VM_ANON_BUSY` serializes pagein, pageout, COW, invalidation, truncate, and
teardown across processes which share an object.  A process-context operation
waits on the anonymous-page address.  A nowait or interrupt-context fault
returns `EWOULDBLOCK`.  A new object-page placeholder is linked before pager
I/O, so two processes cannot instantiate the same object offset independently.

## Address spaces and teardown

- Only the current process mutates its `vmspace`.  `fork` completes the child
  map, pmap, user area, and trap frame before placing the child on the run
  queue.
- `exec` builds a private replacement.  At `splhigh` it switches
  `p_vmspace`, activates the new pmap, and publishes accounting; failure
  restores the old pmap.
- `exit` stops new user faults, waits for busy object pages, removes pmap
  translations, releases object references, and only then publishes the
  zombie.  `wait` reclaims the wired user area and kernel stack.
- The scheduler activates the destination pmap before changing
  `mips_curuser`.  No code retains an address in another process's user area
  across a context switch.

Every MIPS user area has a canary between `struct user` and the descending
kernel stack.  Trap entry and context switching validate it.  KSEG0 cannot
provide an unmapped stack guard on the supported hardware, so this canary and
the trap-frame boundary check are the current guard.
