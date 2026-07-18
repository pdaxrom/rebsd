# Out-of-memory policy

The present small-system policy is bounded failure, not an overcommit OOM
killer.  A physical allocation makes at most three reclaim passes.  Reclaim
skips wired, busy, and referenced pages, writes dirty file pages through their
pager, and uses configured swap for anonymous pages.  Exhausted physical
pages, VM objects, object-page descriptors, swap slots, or map entries return
`ENOMEM`, `ENOSPC`, or the underlying pager error; no path waits forever for
memory which has no producer.

Syscalls propagate the allocation error and leave their old mapping intact.
A user demand fault which cannot be satisfied is delivered to the faulting
process as `SIGSEGV`; vnode or swap I/O failures use `SIGBUS`.  ReBSD does not
select and kill an unrelated process.  Kernel bootstrap allocations remain
fail-fast because the kernel cannot safely continue without its page metadata,
pmap, or process-zero user area.

This deterministic policy is intentional for 4--8 MiB N64 systems.  A future
multi-user OOM selector would need explicit scoring, protected-process rules,
and reclaim progress accounting before it could replace the current behavior.
