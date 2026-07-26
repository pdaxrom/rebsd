# ReBSD port development rules

## No autonomous design gate

Any autonomous invention or deviation from the established ReBSD system is
prohibited.

- Treat the existing machine-independent implementation, documented system
  design, build configuration, filesystem layout, boot policy, ABI, and the
  behavior shared by supported architectures as the required standard.
- Do not invent or infer a replacement design, policy, compatibility mode,
  filesystem, root device, boot source or fallback, image format, ABI,
  permission model, subsystem contract, or architecture-private path.
- Before any departure from the established system, stop work, describe the
  exact proposed departure and its reason, and obtain the user's explicit
  approval.  Silence, convenience, QEMU bring-up, a passing test, or a local
  implementation gap is not approval.
- If the existing standard is missing, ambiguous, or cannot be identified
  after a tree-wide audit, ask the user instead of choosing or deducing one.
- Approval applies only to the exact departure requested.  It does not
  authorize adjacent changes or further assumptions.

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
- A port may add diagnostic tests, but temporary subsystem copies, substitute
  implementations, compatibility shims, and other bring-up workarounds are
  forbidden.  Documenting a workaround or promising to remove it later does
  not make it acceptable.
- Every common-kernel change made by a port must be checked by that port's
  gates and by existing architectures' builds or tests appropriate to the
  change.
- Do not modify or enable PCC while the i686 GCC port is in bring-up.

## No-workaround gate

Workarounds are prohibited for every architecture and board.

- Fix defects at the owning common interface or at the actual
  machine-dependent boundary.
- Do not bypass, duplicate, weaken, stub, fake, hard-code, special-case, or
  suppress a failing contract to make a build or test pass.
- Do not suppress compiler diagnostics instead of correcting the responsible
  common or machine-dependent code.
- Do not disable or relax an existing architecture's build, test, validation,
  security, or correctness gate.
- If the correct implementation cannot yet be completed, leave the feature
  explicitly incomplete and record the blocker; do not merge an interim
  substitute.
