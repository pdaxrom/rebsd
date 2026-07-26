# ReBSD port development rules

## Architecture reuse gate

Every architecture and board port must reuse the existing
machine-independent ReBSD kernel.  Before adding an implementation for any
architecture, search the whole tree for the existing contract and all of its
current users.

- Keep code under `sys/<arch>` limited to that architecture's hardware, boot,
  interrupt and context assembly, ABI, and thin adapters to common interfaces.
- Do not create architecture-private implementations of disk, VFS, filesystems,
  pathname lookup, file descriptors, exec, process lifecycle, signals,
  scheduler policy, syscall tables, libkern, or other common kernel services.
- If a common interface lacks an MD hook, extend the common interface and keep
  every existing architecture's behavior intact instead of copying the
  subsystem.
- Temporary bring-up code must be named and documented as temporary in
  the current port's reuse audit, with a concrete removal condition.  It must
  not be described as a finished production subsystem.
- Every common-kernel change made by a port must be checked by that port's
  gates and by existing architectures' builds or tests appropriate to the
  change.
- Do not modify or enable PCC while the i686 GCC port is in bring-up.
