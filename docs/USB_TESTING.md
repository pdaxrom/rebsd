# ReBSD USB Test Record

This file separates host/build evidence from Creator Ci20 hardware evidence.
Compilation, fake MMIO, or QEMU results are never recorded as JZ4780 hardware
success.

## Automated Gates

Run the complete machine-independent suite with:

```sh
make -C sys/tests/usb test
make -C sys/tests/disk test
make -C sys/tests/fat test
make -C sys/tests/fsck_fat test
make -C sys/tests/mkfs_fat test
```

The 2026-07-15 external-hub candidate passes all USB, disk, and FAT gates:

| Gate | Coverage | Result |
| --- | --- | --- |
| `usb_desc_test` | packed protocol definitions and malformed descriptors | pass |
| `usb_core_test` | pools, addresses, matching, transfer terminal states, cleanup | pass |
| `usb_task_test` | bounded FIFO, coalescing, exhaustion, cancellation, rearm | pass |
| `uhub_test` | external-hub power/reset, interrupt bitmap, child attach/detach, recovery, rapid replug and recursive teardown | pass |
| `ukbd_test` | boot reports, modifiers, repeats, release tracking | pass |
| `ohci_test` | control/periodic ED/TDs, IRQ, RHSC masking, reconnect and keyboard input | pass |
| `ehci_test` | async control/bulk, periodic interrupt-IN, split transactions, atomic QH link/unlink, hub removal/reconnect and companion routing | pass |
| `ci20 usb hw tests` | VBUS, clock, PHY and reset ordering with fake JZ4780 registers | pass |
| `umass_test` | BOT framing/recovery, SCSI probe/capacity, bounded `READ(10)`/`WRITE(10)`, cache flush and command/wire errors | pass |
| `disk_test` | transport-independent MBR parsing, regions/minors, partition-relative writes, dirty tracking and flush errors | pass |
| `fdisk_mbr_test` | exact 512-byte ABI, little-endian fields, range/overflow/overlap validation | pass |
| `fat_test` | FAT16/FAT32 validation, bounded chains, long-name reads, allocation, write/truncate/remove, directory mutation and rename | pass |
| `fsck_fat` smoke | clean/corrupt FAT16/FAT32 images, FAT comparison, directory chains, lost clusters, FSInfo validation and repair policy | pass |
| `mkfs_fat` smoke | FAT16/FAT32 geometry, primary/backup metadata, root initialization and no-write mode | pass |

The OHCI RHSC test explicitly injects another root-hub status event while RHSC
is masked. Reenabling the source must preserve and deliver that event. This is
the regression gate for an unplug/replug race during deferred exploration.

## Target Compiler and Image Gates

The complete Ci20 kernel and rootfs build with both configured MIPS GCC and
PCC profiles. Both images contain `/dev/sd0`, `/dev/sd0a` through `sd0d`,
`/sbin/fdisk`, and its non-empty manual page. The PCC kernel check found no
COP1/FPU instructions.

The hardware-verified GCC read-only mass-storage image has these SHA-256
values:

```text
2c3de12a699b25c68ee599e25196bfa5a17772b3fc964852c3a9997181d68434  unix.elf
5edc69ef9ad0f95a8b33b4f6c6354f55b7b8b8d05870ceaa5687619b1c795a2d  ci20.bin
a9984d94a274a541c6768b40e300399a7bbce05b0fe37aaad143e096058517c0  ci20.uImage
```

On a direct high-speed `1005:b113` flash drive, Ci20 reported 30,299,520
512-byte sectors, parsed an MBR type `0x0c` partition starting at sector 63,
and completed 16 KiB reads through both `/dev/sd0` and `/dev/sd0a`. Idle
detach removed `sd0` and `umass0`; reconnect reused USB address 1 and disk unit
0, after which all reads succeeded again. The retained UART capture is
`usb-logs/ci20-umass-readonly-verified-20260713.txt`.

This verifies the read-only raw-block portion of Phase 10.

The later read-only FAT32 image mounted `/dev/sd0a` on `/mnt`, listed short and
long names, traversed nested directories, and exposed a 32 MiB file. A write
attempt failed with `Read-only file system`, as required. The retained UART
capture is `usb-logs/ci20-fat32-readonly-verified-20260713.txt`. A separate
16 MiB `dd` read of that file completed successfully in the same hardware
session; the short command/result excerpt was supplied separately and is not
present in the retained full capture.

The GCC kernel linked against the previously verified 32 MiB Ci20 rootfs and
was packaged as:

```text
../rebsd-usb-support-build/ci20-kgcc-ugcc-mips32r2-hard-little-elf/obj/sys/mips/ci20/ci20.uImage
```

The first hotplug image had these SHA-256 values:

```text
7c4c9ffa575cd9ed03b40189a61bbde302a43e3f79bcad0ac9540bfcd3de2a45  unix.elf
a015dfa4198e4d1d688b036c8ccfcbf900f3cbe4db98580919365443e49dcf1c  ci20.bin
40e15f420336d89088439ac0f2e7831eaa94908f40b70be7624f6bdfe7b9b887  ci20.uImage
```

That image is failed, must not be reused, and was not committed. With a
keyboard present at boot it reached a working login, but init printed
`WARNING: Something is hung (wont die)`; unplug then produced an interrupt
I/O error and `panic: wakeup`. The worker was incorrectly parented to init,
whose startup shutdown/wait pass cannot reap a permanent `SSYS` child. The
raw capture is `usb-logs/ci20-ohci-hotplug-failed-worker-20260713.txt`.

The proc0-parented second candidate had these SHA-256 values:

```text
9706670589213e2abe764be124930451ce33d7d01762384793469f1a89dfb5b7  unix.elf
5a4df9c3e0bc8fa79ad51f69b0f80823141f59b65d00727c1a71eb56d66872b6  ci20.bin
33e203e3ade3d4a60fcfa81bbd7187c2f5fc7aa751ea91e49b408a9a90c01747  ci20.uImage
```

That image is also failed, must not be reused, and was not committed. It fixed
the init warning and wakeup panic, and printed `task worker pid=2 parent=0
ready`. On unplug it reported the keyboard interrupt I/O error but no
root-hub detach. The periodic WDH error path had disabled MIE before the
hardware asserted the later RHSC. The raw capture is
`usb-logs/ci20-ohci-hotplug-failed-mie-20260713.txt`.

The third candidate, which preserved MIE while RHSC was armed, had these
SHA-256 values:

```text
bc651d3cc52fde88351bda638e437de96923e8b7cea73e53a6caacc7d071174d  unix.elf
5397788adf24112779474ba5dc32c649b5fcfc0b313dbf2a2e86d5c74973d038  ci20.bin
defcb353ce302f0673dc0685d1196224b3798ffa89bc2d8c21e03c671f52720b  ci20.uImage
```

That image is failed, must not be reused, and was not committed. It reached
`login:` with the keyboard attached, but both USB and UART input were
unresponsive. The absence of an explicit register dump means IRQ5 starvation
is an inference from the simultaneous loss of both inputs, not a directly
captured status value. The raw capture is
`usb-logs/ci20-ohci-hotplug-failed-input-starvation-20260713.txt`.

The deferred-probe candidate with IRQ-storm quarantine has these SHA-256
values:

```text
a8bec837fd17fda2c5b34407217318287504b76c7fe18a3195b1e3d88fa9add8  unix.elf
f9f822f8c7db9d23cda6cac11e7341f16c645dbc5496f18d409b6fb19387382a  ci20.bin
b86963ff8bc2c1193c8f3fe3ff6f95b9151e5ff4553c05e1853212460be8ae79  ci20.uImage
```

That fourth image is failed, must not be reused, and was not committed. It
accepted input at login, but unplug produced the periodic I/O error and then
`panic: wakeup` when UART input caused another wakeup. The IRQ-storm guard
did not fire. Together with the missing PID 2 in `ps`, this identifies the
`newproc()`-based SSYS child and its stale sleep-queue entry as the remaining
failure. The raw capture is
`usb-logs/ci20-ohci-hotplug-failed-stale-worker-20260713.txt`.

The candidate with no USB process and a proc0 queue runner has these SHA-256
values:

```text
0a4218c4fc4d6972c5edabd56b355a2a9899872760d88ec1e22819788bda1faf  unix.elf
d13c089bc55035b77945e047ae7dfeef09b69d525f46b2b12e1667c847c889f4  ci20.bin
5d2b3249bd83331e7a3e871de33fb9fb58db7a22a44c6d82665ac005118695fa  ci20.uImage
```

This image passed the Creator Ci20 hotplug gate on 2026-07-13. With a keyboard
present at boot it detached and reattached without a panic, reused USB address
1, and accepted `ls` through the reconnected keyboard. With the port empty at
boot it reported `hotplug ready`, attached after the login prompt, accepted
the login through USB, and survived more than five detach/reconnect cycles.
The raw captures are
`usb-logs/ci20-ohci-hotplug-proc0-preconnected-20260713.txt` and
`usb-logs/ci20-ohci-hotplug-proc0-boot-empty-20260713.txt`.

The fourth EHCI hardware-test candidate had these SHA-256 values:

```text
67507395d4c2ee72ba43ec6790e4c8317514abc86214046e9543d8b8ff092b1e  unix.elf
e2d77c5cdeb2174f8af556410ed76642010879f7fdc0c65ad9dd0d02aae4a1df  ci20.bin
5d27dc46cead8056fb671677c299bfbd65ac705c8445740b995d649d482ffa07  ci20.uImage
```

The first candidate `7f6df5c7...` reached login with J23 empty and verified
real EHCI capabilities, IRQ 20, hotplug readiness, and a low-speed keyboard
handoff to OHCI. EHCI enumeration attempts returned `I/O error`, so the image
did not pass the high-speed gate. The retained raw capture is
`usb-logs/ci20-ehci-first-candidate-20260713.txt`.

The second image `eb9a8976...` produced the same enumeration failure with a
diagnostic. `PORTSC=0x1005` proved a connected, enabled high-speed port, while
qTD token `0x00080248` proved that the first eight-byte SETUP exhausted its
three transaction attempts; the data and status qTDs were still active. Its
raw capture is `usb-logs/ci20-ehci-second-candidate-20260713.txt`.

The third image `f21e339d...` added `USB_PORT_RESET_RECOVERY` before the first
SETUP and `USB_SET_ADDRESS_SETTLE` before traffic at the new address. It still
failed the first address-zero `GET_DESCRIPTOR` with the same transaction
error. The retained raw capture is
`usb-logs/ci20-ehci-third-candidate-20260713.txt`.

The fourth image was not committed and was not the verified image. It retained
the timing and descriptor fixes but changed only the board UHC clock path to
match Ci20 Linux: MPLL 1.2 GHz divided by 25 to 48 MHz, with expected final
`UHCCDR=0x60000018`. Host fake-MMIO tests assert that exact clock value and
still cover high-speed enumeration, bulk IN/OUT, short transfer, stall,
timeout, abort, companion handoff, reclaim, and later high-speed attach. They
did not prove the corrected path on JZ4780 hardware.

The final EHCI image has these SHA-256 values:

```text
8f50baaddc188dc1c47a82c85cb63e7cf88f288edbc0ccff50ab577f157e8f82  unix.elf
abcd22af3f43ce5c83a63f9f75b0db05a51da7f4a439c0d353661b3e7991a2ca  ci20.bin
05adb071ddd9b12f584192e80542173b7ad91cb885f99d6a3f25c98b284025fa  ci20.uImage
```

This image sets both shared-PHY interface-width bits and produced
`USBPCR1=0x8ace3370`. A direct high-speed `1005:b113` flash drive enumerated
as class 8, subclass 6, protocol 80 with two endpoints, then detached and
reattached at address 1. A direct low-speed `1c4f:0002` keyboard was handed
from EHCI to OHCI, supplied console input, detached, and reattached without a
panic. The raw captures are
`usb-logs/ci20-ehci-high-speed-verified-20260713.txt` and
`usb-logs/ci20-ehci-companion-verified-20260713.txt`.

The legacy AWK out-of-tree rule was corrected to request and consume the
header name generated by `byacc -d -o awk.g.c`. Clean GCC and PCC rootfs builds
now complete, and `fsutil --check` passes for both generated images.

## External Hub and Split-Interrupt Gate

The current implementation supports a high-speed external hub, high-speed
bulk children, and low/full-speed interrupt children through the hub's
transaction translator. The hardware topology used a `214b:7000` four-port
high-speed hub with a `1005:b113` flash drive and a low-speed `1c4f:0002`
boot keyboard attached simultaneously. The final image is:

```text
861ae15e0ba79af9a2a6364c66f593656fcbf8c6274626b49425f437002e66fa  ci20.uImage
```

The stable fix atomically links and unlinks periodic QHs without cycling the
complete EHCI periodic schedule. A split interrupt QH is removed before its
single fixed qTD/overlay is rearmed, then linked back after the new transfer
is fully published. This avoids JZ4780 `MISSEDMICRO` failures while bulk and
hub traffic are active. Recursive topology teardown releases keyboard,
storage, and hub state before the root device is reused.

The final `861ae15e...` image passed the hardware gate on 2026-07-15. The raw
capture shows simultaneous keyboard and storage enumeration, four keyboard
child detach/reattach recoveries, flash child reconnects, five complete
populated-hub disconnect/reconnect cycles, working console input after the
cycles, and a final read-only FAT32 mount and directory listing. Transfer or
enumeration errors printed while a child or the complete hub is physically
being removed are followed by detach and successful fresh enumeration. The
retained capture is
`usb-logs/ci20-ehci-hub-reconnect-verified-20260715.txt`.

## Writable USB Block Gate

The writable mass-storage candidate has these SHA-256 values:

```text
8162eeb0d73c04658bcf23c4074f3b96e7c69bde2c22dfffdc601f3d0a94ad21  unix.elf
e0dacf6b0550d52afb9420422596b7acbd72071d1f1f9c0db529e6d817d95923  ci20.bin
e2b14a2245d01a9b61069b31c92104ecad18c6dea6c4ccab4c0274febca9f9a8  ci20.uImage
```

On 2026-07-15 the `1005:b113` high-speed flash drive and `1c4f:0002`
low-speed keyboard enumerated simultaneously behind the `214b:7000` hub. The
test saved the unused whole-disk range at sectors 32 through 61, overwrote it
with a 15 KiB pattern, called `sync`, read it back, and passed `cmp`. It then
restored the saved bytes, called `sync`, read them back, and passed a second
`cmp`. Finally `/dev/sd0a` mounted as read-only FAT32 and its directory listing
succeeded, proving the partition remained intact. The retained UART capture
is `usb-logs/ci20-umass-write-verified-20260715.txt`.

This gate verifies raw block writes through BOT, the disk close/flush path,
readback, and byte-exact restoration. Later hardware gates verified the FAT
write path itself.

## Writable FAT, off64, and Maintenance-Tool Gates

On 2026-07-15 `/dev/sd0a` mounted as FAT32 read-write behind the populated
high-speed hub. The board created and compared regular files, truncated and
rewrote them, removed them, created nested directories, rejected removal of a
non-empty directory, removed empty directories, renamed regular files and a
non-empty directory, synchronized, remounted, and verified persistent results.
A separate `-r` mount rejected creation. The scripted rename gate ended with
`fat rename smoke ok`.

The 64-bit seek smoke passed against `/dev/sd0` using both the prebuilt target
GCC program and a program compiled on the board by PCC. It seeks beyond 2 GiB
without truncation and ended with `off64 smoke both compilers ok`. This is an
ABI/file-offset result; the legacy UFS on-disk format remains 32-bit and is a
separate future filesystem version.

`fs-tools-smoke.sh` then created a sparse 4 MiB image, formatted it FAT16, and
checked it clean. `mkfs.fat -N -v /dev/sd0a` reported the existing FAT32
geometry without writing, and the corrected `fsck.fat -n /dev/sd0a` reported
500 files, 47 directories, 1,869,971 free clusters, zero bad clusters, and no
uncorrected errors. The verified image SHA-256 is:

```text
0b357a655e984a18161f7355b6b362e912fa83b7908e0778a3d8657b16555720  ci20.uImage
```

The exact supplied console transcript is retained in
`usb-logs/ci20-storage-filesystem-verified-20260715.txt`.

## Creator Ci20 Matrix

| Test | Device/topology | Status | Evidence |
| --- | --- | --- | --- |
| polling enumeration and DM9000 coexistence | direct low-speed `1c4f:0002` | verified 2026-07-13 | UART supplied during bring-up |
| boot keyboard input | direct low-speed `1c4f:0002`, present at boot | verified 2026-07-13 | `usb-logs/ci20-ohci-keyboard-20260713.txt` |
| post-boot disconnect/reconnect | direct low-speed keyboard on J23 | verified 2026-07-13 on `5d2b32…` | both successful captures; earlier failures retained |
| boot input with hotplug code | direct low-speed keyboard on J23 | verified 2026-07-13 on `5d2b32…` | preconnected successful capture |
| boot empty, attach after login | direct low-speed keyboard on J23 | verified 2026-07-13 on `5d2b32…` | boot-empty successful capture |
| five reconnect cycles | direct low-speed keyboard on J23 | verified 2026-07-13 on `5d2b32…` | boot-empty successful capture |
| specifically timed rapid unplug/replug | direct low-speed keyboard on J23 | not separately tested | retain as later stress gate |
| EHCI start, capabilities, IRQ 20, empty-port hotplug | right-hand J23 empty | verified 2026-07-13 on `7f6df5…` | first EHCI capture |
| EHCI to OHCI low-speed handoff | direct `1c4f:0002` keyboard on J23 | verified 2026-07-13 on `05adb0…` | final companion capture |
| full-speed device | right-hand J23, direct companion OHCI | implementation candidate, not tested | EHCI/OHCI routing hardware gate |
| external high-speed hub | downstream low-speed keyboard plus high-speed flash | verified 2026-07-15 on `861ae1…` | full UART boot, attach, input, storage, FAT32, and reconnect capture |
| hub child reconnect | keyboard, flash, and five complete hub reconnects | verified 2026-07-15 on `861ae1…` | `ci20-ehci-hub-reconnect-verified-20260715.txt` |
| high-speed flash drive | right-hand J23, direct EHCI | verified attach, detach, and reconnect 2026-07-13 on `05adb0…` | final high-speed capture |
| read-only USB block device | direct high-speed `1005:b113` on right-hand J23 | verified 2026-07-13 on `a9984d…` | capacity, MBR, whole/partition reads, idle detach/reconnect and reuse capture |
| writable USB block device | high-speed `1005:b113` behind `214b:7000` hub | verified 2026-07-15 on `e2b14a…` | 15 KiB write/read/compare, byte-exact restore/compare, then FAT32 integrity check in `ci20-umass-write-verified-20260715.txt` |
| FAT32 read-only baseline | `/dev/sd0a`, MBR type `0x0c` | verified 2026-07-13 | mount, traversal, long names and enforced read-only behavior in `ci20-fat32-readonly-verified-20260713.txt` |
| FAT32 read-write filesystem | `/dev/sd0a` behind `214b:7000` hub | verified 2026-07-15 | file/directory mutation, rename, sync/remount persistence, and `-r` enforcement |
| 64-bit file offsets | `/dev/sd0`, target GCC and native PCC | verified 2026-07-15 | both compiler paths passed seeks beyond 2 GiB |
| `mkfs.fat` and `fsck.fat` | scratch FAT16 image and existing `/dev/sd0a` FAT32 | verified 2026-07-15 on `0b357a…` | `fs-tools-smoke.sh`, no-write geometry probe, clean non-mutating check |
| OTG port in host mode | left-hand J24/J8 | not implemented | separate DWC2 phase |

The exact current hardware procedure and expected log lines are in
`docs/CI20_USB.md`. At minimum, confirm the proc0 runner line, init PID 1,
and absence of a `usbtask` process; verify that detach does not crash or
stall, confirm that reconnect returns a working keyboard, and test a keyboard
first connected after login.

## UART Log Policy

Store a raw UART capture under `docs/usb-logs/` for every claimed hardware
gate. Record the board, device ID, detected speed, direct/hub topology, image
SHA-256, commands typed through USB, and the result. Do not replace a failing
capture with expected output. Change only the matrix rows directly proved by
the retained capture.
