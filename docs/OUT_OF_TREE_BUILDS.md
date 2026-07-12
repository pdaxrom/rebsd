# Out-of-tree builds

MIPS builds keep generated files outside the source tree.  Invoke a board
through the shared architecture entry point and select an object root with
`O`:

`O` is the uppercase Latin letter O (for object/output), not the digit zero
`0`.  The assignment syntax is `O=/path/to/build`.

```sh
make -C sys/mips BOARD=malta64 O=/work/rebsd-malta64 all
make -C sys/mips BOARD=maltael O=/work/rebsd-maltael all
make -C sys/mips BOARD=n64 O=/work/rebsd-n64 \
    N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc all
```

Do not invoke `make` in `sys/mips/<board>`.  The board `Makefile`, `ioconf.c`,
and `swapunix.c` are kconfig outputs and are generated under the selected
object root.

If `O` is omitted, the dispatcher uses a sibling directory named
`../retrobsd-build/<profile>`.  The profile includes board, kernel compiler,
userland compiler, CPU, floating-point ABI, endian, and executable format.
`BUILD_PROFILE=name` can override only the automatically selected directory
name; an explicit `O` always wins.

The object root has these ownership boundaries:

```text
O/obj/        mirrored compiler objects, generated code, images and rootfs
O/toolchain/  host and target PCC build/install trees
O/tests/      regression outputs and QEMU logs
O/tmp/        temporary files exported through TMPDIR
```

Separate configurations must use separate `O` values.  They may then build
and run concurrently.  `clean` affects only the selected object root.  Source
files, checked-in documentation, and build products in another profile are not
removed.

Parallel make is supported within one object root as well:

```sh
make -j4 -C sys/mips BOARD=malta64 O=/work/rebsd-malta64 all
gmake -j4 -C sys/mips BOARD=maltael O=/work/rebsd-maltael all
```

The recursive runtime and generator rules keep `clean`, generation, build,
and install phases ordered while allowing independent objects and directories
to use the make jobserver.

One `make -jN` process owns one object root.  Do not run two independent make
processes against the same `O`; give each process a different `O` path.  This
restriction does not prevent parallel jobs within a single `make -jN` build.

Use object paths without whitespace.  The dispatcher preserves argv without
`eval`, but legacy compiler, linker, and kconfig recipes do not yet quote every
path expansion.

The build requires GNU make syntax already used by the ReBSD tree.  The
dispatcher and recursive rules are tested with Apple GNU Make 3.81 and GNU
Make 4.4.1, including fresh full `-j4` builds.  They do not depend on a
particular executable name: invoking `gmake` keeps `gmake` for recursive
builds.  BSD make support would require a separate conversion of the existing
GNU make conditionals and functions.

## Native PCC

PCC binaries that run inside ReBSD are built by
`sys/mips/tools/Makefile.native-pcc`.  It has explicit targets for generated
configuration headers, yacc/flex sources, host `mkext`, target objects, and
the native `cc`, `cpp`, and `ccom` binaries.  Malta and N64 pass their ABI and
tool paths to this Makefile; no Python build driver is used.

Python programs under `sys/mips/tools` remain test and image orchestration
tools.  Their outputs and logs are directed to the selected object root.
