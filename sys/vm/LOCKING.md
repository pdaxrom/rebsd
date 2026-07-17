# VM locking and lifetime rules

The phase-4 VM runs on the existing uniprocessor kernel.  There are no
sleeping VM locks yet: interrupt priority provides the short publication
barrier, while ownership prevents concurrent mutation of maps and pmaps.

## Address spaces and process state

- A live process owns exactly one `vmspace`; a `vmspace` is not shared in
  phase 4.  Only the current process may change its map.
- `fork` clones the parent's map while the parent is current.  The child is
  placed on the run queue only after its vmspace, user area, trap frame, and
  process fields are complete.
- `exec` constructs and loads a private replacement vmspace first.  At
  `splhigh`, it switches `p_vmspace`, activates the replacement pmap, and
  publishes the new register/accounting state.  Failure before publication
  destroys only the replacement; activation failure restores the old pmap.
- `exit` destroys the current process mappings before publishing the zombie.
  `wait` reclaims the zombie's wired user area and kernel stack.
- The scheduler activates the destination pmap before changing
  `mips_curuser`.  No code may retain an address inside another process's
  user area across a context switch.

## Map, pmap, and page operations

- `vm_map` mutations and the corresponding `pmap` changes are performed by
  one owner and may not sleep.  A failed operation must leave the old map
  intact or roll back pages inserted by that operation.
- Physical-page allocation and pmap ASID/TLB updates use their existing
  interrupt exclusion internally.  Callers do not hold an interrupt barrier
  while performing filesystem I/O for `exec`.
- The current order is process publication barrier, then vmspace/map, then
  pmap, then physical pages.  Code must release that chain before inode,
  buffer-cache, or device operations.  Object, vnode, and swap locks will be
  added and fitted into this order with the phase-5 pagers.

Every MIPS user area has a canary between `struct user` and the descending
kernel stack.  Trap entry and context switching validate it.  Direct-mapped
KSEG0 stacks cannot have an unmapped hardware guard page, so the canary and
the trap-frame boundary check are the available guard on current hardware.
