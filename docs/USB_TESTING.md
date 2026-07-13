# ReBSD USB Test Record

This file separates host/build evidence from Creator Ci20 hardware evidence.
Compilation, fake MMIO, or QEMU results are never recorded as JZ4780 hardware
success.

## Automated Gates

Run the complete machine-independent suite with:

```sh
make -C sys/tests/usb test
```

The 2026-07-13 hotplug candidate passes all seven gates:

| Gate | Coverage | Result |
| --- | --- | --- |
| `usb_desc_test` | packed protocol definitions and malformed descriptors | pass |
| `usb_core_test` | pools, addresses, matching, transfer terminal states, cleanup | pass |
| `usb_task_test` | bounded FIFO, coalescing, exhaustion, cancellation, rearm | pass |
| `uhub_test` | initial/late attach, deferred detach/reconnect, rapid replug, address reuse | pass |
| `ukbd_test` | boot reports, modifiers, repeats, release tracking | pass |
| `ohci_test` | control/periodic ED/TDs, IRQ, RHSC masking, reconnect and keyboard input | pass |
| `ci20 usb hw tests` | VBUS, clock, PHY and reset ordering with fake JZ4780 registers | pass |

The OHCI RHSC test explicitly injects another root-hub status event while RHSC
is masked. Reenabling the source must preserve and deliver that event. This is
the regression gate for an unplug/replug race during deferred exploration.

## Target Compiler and Image Gates

The changed kernel translation units compile with the configured MIPS GCC.
`usb_task.c`, `uhub.c`, `ohci.c`, `usb_service.c`, `sys/mips/ci20/usb.c`,
`init_main.c`, `vm_sched.c`, and generated `ioconf.c` also pass the MIPS PCC compile gate;
the PCC kernel check found no COP1/FPU instructions.

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

A forced full userland rebuild currently stops in the legacy awk build because
`awk.lx.c` includes `awk.h` before the make rules generate that header. This
failure is outside the USB sources; all changed kernel units compile and the
candidate kernel links. It must not be reported as a clean full-tree build.

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
| full-speed device | direct | not tested | later hardware gate |
| external hub | downstream keyboard | not implemented | later hub phase |
| high-speed flash drive | right-hand J23, direct EHCI | not implemented | EHCI phase |
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
