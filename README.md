# ReBSD

Current release: **0.1-Resurgence**.

ReBSD is a fork of RetroBSD with MMU support, ported to Nintendo 64 and
vintage MMU-enabled MIPS hardware.

The original RetroBSD project remains the historical base for this source tree.
See `docs/ORIGIN.md` for attribution and compatibility policy.

## Version reporting

The uname -a command follows the compact BSD layout and reports the release,
kernel configuration, build number and date, and machine class. Build
provenance is available through read-only sysctl nodes instead:

    kern.codename              hw.cpu
    kern.compiler              hw.fpu
    kern.builduser             hw.byteorder
    kern.buildhost
    kern.build
    kern.toolchain
    kern.toolchain.version
    kern.gitrev
    kern.branch
    kern.dirty
    kern.buildinfo

The kern.buildinfo node is the convenient multi-line summary. The build
number defaults to 1 and may be set with REBSD_BUILD_NUMBER; reproducible
builds may supply REBSD_BUILD_DATE.

All supported 32-bit architectures use the native signed 64-bit `time_t` ABI.
The ABI and the unchanged legacy UFS disk-format boundary are documented in
[docs/TIME64.md](docs/TIME64.md).

## Source Roadmap

    bin         User commands.
    etc         Template files for /etc.
    include     System include files.
    lib         System libraries.
    libexec     System binaries.
    sbin        System administration commands.
    share       Shared resources.
    sys         Kernel sources.
    tools       Build tools and simulators.


## Supported hardware

- Nintendo 64 / NEC VR4300.
- QEMU Malta with 32-bit big-endian MIPS.
- QEMU Malta/R4000 with the VR4300 MIPS-III profile and 32-bit user ABI.
- QEMU Malta with 32-bit little-endian MIPS.
- Ingenic JZ4780 / CI20 (build-only gate).


## Build

All supported boards use out-of-tree builds. For example:

```shell
make -C sys/mips BOARD=malta64 O=/work/rebsd-malta64 all
make -C sys/mips BOARD=n64 O=/work/rebsd-n64 \
    KERNEL_COMPILER=gcc USERLAND_COMPILER=pcc all
```

`O` is the uppercase Latin letter O (for object/output), not the digit zero
`0`.  When `O` is omitted, a configuration-specific directory is created
under the sibling `../retrobsd-build` directory.  See
[docs/OUT_OF_TREE_BUILDS.md](docs/OUT_OF_TREE_BUILDS.md) for artifact paths,
parallel builds, cleanup, and GNU make compatibility.

The build requires a MIPS cross GCC toolchain, Berkeley YACC, flex, groff, and
the host libraries used by the build tools. On Ubuntu, install the host tools
with:

```shell
sudo apt-get install byacc bison flex groff-base libelf-dev
```

On macOS:

```shell
brew install byacc bison flex groff libelf
```

Parallel builds are supported inside one object directory:

```shell
make -j4 -C sys/mips BOARD=malta64 all
```

The generated kernel, root filesystem, native toolchain, and test logs are
stored under the selected `O` directory. Board-specific run and test targets
are documented in [sys/mips/README.md](sys/mips/README.md),
[sys/mips/n64/README.md](sys/mips/n64/README.md), and
[docs/TOOLCHAIN.md](docs/TOOLCHAIN.md).
