# i686 legacy-port completion checklist

Status: software-complete on branch `finish_legacy_port`, 2026-08-10.
The primary i686, Ci20 and N64 Expansion Pak hardware candidates were
subsequently reported passing their operator-run acceptance checks on
2026-08-10. N64 acceptance was completed after reboot was changed to re-enter
the production ROM IPL3 warm path.

This checklist closes the currently declared ReBSD target: a uniprocessor
i686 PC with legacy BIOS, Linux/x86 boot protocol 2.02 or GRUB Legacy,
8259A/PIT interrupt hardware, VGA text/COM1 console, PS/2, PCI INTx,
PIIX/VIA IDE, UHCI/OHCI/EHCI USB, MC146818 RTC and RTL8169 Ethernet.

The embedded read-only UFS root and writable `/var` RAM disk are established
boot policy.  Writable IDE/USB partitions remain additional filesystems and
swap devices; changing the root policy is not part of this work.

## Completion work in this branch

- [x] Move the filesystem phase of the existing ReBSD `boot(dev, howto)`
      contract into its common owner and use it from i386, Ci20, N64 and
      Malta without changing their MD terminal actions.
- [x] Implement and gate the supported legacy-PC halt action: synchronize
      writable filesystems, disable interrupts and enter HLT.
- [x] Keep reset and poweroff explicitly unsupported on the legacy PC target:
      synchronize, report the unsupported reboot request and stop in HLT.
      Selecting an i8042 reset pulse or APM/ACPI poweroff is a separate
      hardware-policy change, not a legacy-port workaround.  Ci20 has its own
      JZ4780 watchdog/RTC terminal actions and is not the PC policy owner.
- [x] Enable the existing common PTY service and `/dev/tty` driver with the
      same device ABI and four-unit policy used by supported ReBSD boards.
- [x] Enable the existing common Unix-domain socket service without adding an
      i386-private socket implementation.
- [x] Run `ptytest` and an AF_UNIX stream/datagram smoke inside the normal
      i686 root filesystem.
- [x] Replace the placeholder i686 kconfig inventory with the complete common
      kernel source inventory used by the image, while retaining only i386
      hardware and ABI sources under `sys/i386`.
- [x] Add an early CPUID qualification/report for the actual `-march=i686`
      contract and preserve the optional, separately checked SSE context
      mode.
- [x] Restore deterministic exception gates for divide error, general
      protection and write-protection page fault on the production IDT/trap
      path; diagnostic gates must not create an alternate kernel subsystem.
- [x] Add a bounded QEMU stability gate covering repeated process creation,
      VM pressure, filesystem I/O, IDE DMA, USB/input and network loopback.
- [x] Update `sys/i386/README.md`, `docs/I386_MD_API.md` and historical status
      notes to describe the current GCC-kernel, GCC/PCC-userland and native
      PCC rootfs support accurately.

## Automated acceptance gates

- [x] Strict i686 toolchain check and clean default kernel/rootfs build.
- [x] Direct Linux/x86-protocol QEMU boot gate for the standard
      `rebsd-i686.bzimg` artifact.
- [x] Remove the obsolete raw-floppy artifact, native CHS loader and all
      `bios-*` QEMU targets with owner approval: the standard artifact is
      25,491,268 bytes while that format was limited to 1,474,560 bytes.
      GRUB Legacy loading `rebsd-i686.bzimg` remains the sole physical BIOS
      boot contract.
- [x] 32/64/128/256/768/1024 MiB QEMU memory matrix.
- [x] IDE forced PIO, forced DMA, automatic mode and absent-device gates.
- [x] PS/2, UHCI, OHCI, EHCI, USB mass-storage and combined activity gates.
- [x] FAT read/write/repair, runtime RAM disk and swap gates.
- [x] Native PCC host/runtime smoke suite.
- [x] Host VM, disk, DMA, PCI, RTC, console, FAT and USB tests.
- [x] Existing Ci20, N64 and Malta kernel-object builds required by the
      common-kernel change made here.

## Hardware acceptance

- [x] Repeat the complete image on IBM 6563-W4G, including synchronized halt,
      the documented unsupported reboot/poweroff result, VIA UDMA4, RTL8169
      traffic, PS/2 and VIA UHCI activity.
- [x] Verify cold boot and persistent filesystem/RTC state after power loss.
- [x] Run the Ci20 halt, watchdog reboot, RTC poweroff/wake and persistence
      checks on physical hardware.
- [x] Repeat the final N64 GCC-kernel/PCC-userland physical acceptance after
      the active CDC ECM controller is quiesced and reboot re-enters the
      production ROM IPL3 warm path.
- [ ] Run the legacy image on a second physical BIOS i686-class PC if one is
      available.
- [ ] Record complete serial/VGA logs and any chipset-specific failure before
      changing the supported hardware contract.

The optional second-PC run and archival log import remain follow-up evidence;
they do not block the accepted primary hardware targets.

## Explicitly deferred post-legacy work

These are not silently selected or designed by this branch:

- ACPI table/resource/power core and its common/MD ownership contract;
- Local APIC, I/O APIC, MSI/MSI-X, HPET and TSC timecounter policy;
- SMP and the required common scheduler, VM, network, buffer-cache and USB
  synchronization work;
- UEFI, PAE/highmem/NX, SATA/AHCI, NVMe, xHCI, framebuffer/DRM and audio;
- additional IDE channels/devices, ATAPI/LBA48 and additional Ethernet
  controllers;
- any change to the embedded UFS root policy, filesystem ABI or boot-source
  selection.

Work on any deferred item requires a separate tree-wide contract audit and
explicit approval of its exact system design.
