# PCC Phase 5 Report

## Phase 5A: CFG And SSA Verification

Phase 5A prepares the existing pass2 representation for incremental scalar
optimization.  It does not enable SSA in normal compilation, add a new IR, or
change target code generation.  The implementation is machine-independent and
does not enlarge the MIPS backend.

The new `mip/cfgverify.c` checks:

- complete, contiguous, and non-overlapping basic-block coverage;
- block numbering and the label-to-block map;
- exact direct, conditional, computed-goto, and fallthrough successors;
- reciprocal predecessor/successor edge multiplicity;
- DFS parents and independently recomputed immediate dominators;
- dominator-tree child bitsets;
- phi predecessor count, duplicate destination TEMP, and Aphi membership;
- phi source/destination TEMP ranges before and after SSA rename.

`optim2.c` now uses one build-and-verify helper at the initial and post-SSA CFG
construction points.  Normal `xtemps` compilation runs the allocation-free CFG
checks.  Independent dominator bitsets and phi checks are allocated only when
the existing internal `-xssa` path is explicitly selected.

The common verifier object is included in C, C++, F77, split C pass, host-cross,
and native PCC builds.  The permanent host smoke compiles a nested-loop and
call probe with `-Wc,-xssa` so the dormant dominator/phi lifecycle remains
exercised.

## Assembly Identity

A MIPS32r2 hard-float compiler built from pre-change commit `ce61008c` was
compared against verifier commit `3f6f628e` with identical flags.  These files
were byte-identical:

| Source | Assembly SHA-256 |
| --- | --- |
| `optim003.c` | `655c3497f6171e827f9de78eb3e1cc46d2520eef8287c0aea1801af7d3f40c43` |
| `spillcost001.c` | `549bcb6b68553e28f38ad2bc3c347dc7200e7966fc5dfffddfe26be0f3bcc123` |
| `spillfp001.c` | `0b5c4769719c037d8357946fb1d204dca01e22cafa6381e64d123c20f2032ef1` |
| `spillabi001.c` | `a6ddc4f3bf4170193da291a1f5c42c2b56e58a854dfc403c70221a0dbee2a30f` |
| `extendsfdf2.c` | `1595bf4b68aba40512f2e731e25b857af5b06d254a1eff4cf70871fc0be3759a` |
| `linpack.c` | `61f071066b8b537dd764d313030a655ba92f82f1943be750dcce08f7727e9098` |

VR4300 hard-float Linpack was also identical: `79974` assembly bytes and
SHA-256 `ab7f5876e2d2fb599cc1674d8a1298c44972afc2d61b37b6fb3f21935a869b86`.

The stripped cross-host `ccom` size changed from `526104` to `542712` bytes,
an increase of `16608` bytes or about 3.2 percent.

## Validation

Commands used:

```sh
make -C sys/mips/malta smoke-host-portablecc
make -C sys/mips pcc-regress-compile
make -C sys/mips pcc-regress-runtime
make -C sys/mips/malta native-pcc-regress-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips/n64 pcc-debug-image
```

Cross regression compiled 322 of 325 tests with only the existing x86 inline
assembly, shared-library/PIC, and TLS expected failures.  All 285 runtime tests
passed.  Native PCC compiled 295 tests, observed the 30 existing expected
failures, and passed all 285 runtime tests.  Unexpected compile and runtime
failures were both zero, with `NATIVE_PCC_REGRESS_RC:0`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11571 / 11473 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 924 / 908 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 13413 / 13568 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 883 / 895 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13378 / 13679 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 911 / 908 | `PCC_SMOKE_ALL_RC:0` |

Every profile also reported `PCC_SMOKE_ALL_FAILURES 0`.  Three isolated reruns
of the identical Malta64 hard image ranged from 12880 to 13230 KFLOPS.  The
lower result after the full compiler stress workload is runtime noise rather
than a code-generation difference.

## N64 Artifact

The hard-float a.out hardware image is:

```text
sys/mips/n64/builds/20260710-phase5a-cfg-verifier/pcc-debug.z64
verifier source commit: 3f6f628e
size: 6619136 bytes
sha256: 3ce8f732949fb0d885b6e1bc55c95663aafeff45cfd55f338dd240a4b17ce806
cross pcc sha256: 9c490a935ead5fa5819733227a6b62ce31f1e322e5155cce1b7a1fa1dcbc7065
cross ccom sha256: 8363b14fbcece51d54fb02373a29e34b5b0134ecbe319fc8b769fd705bc2ca8b
native ccom sha256: d6d5e37a8bb880669931ea0f22e5bd582ceffcab0b10ede136a5ee2d24e61fbc
```

The earlier N64 build directories remain byte-for-byte unchanged.  Real N64
hardware completed the image with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

This validates the Phase 5A native compiler and default `-mfix4300` hard-float
a.out path.  It does not cover a soft-float image or a distinct
`-mno-fix4300` run, and QEMU still cannot reproduce the physical multiply
erratum.

## Next Step

Phase 5B must implement safe critical-edge splitting and parallel-copy phi
lowering before any new SSA scalar transformation is enabled.  That work must
remain a separate commit and retain the existing fallback path.  Copy
propagation and dead-code elimination begin only after Phase 5B passes the same
cross/native and six-profile gates.
