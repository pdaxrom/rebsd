# Ci20 host tools

This directory contains the host-side tools used to debug ReBSD on the
Creator Ci20 through an FT2232D:

- `ci20-ftdi-uart` opens FT2232D channel B as a raw 115200 8N1 UART without
  resetting the USB device or disturbing OpenOCD on channel A;
- `openocd/ci20-xburst.cfg` describes the JZ4780 TAP, core selection and safe
  probe/snapshot commands;
- `openocd/patches/0001-ci20-xburst-jtag.patch` carries the required fixes on
  top of the pinned OpenOCD-XBurst revision;
- `openocd/build-openocd-xburst.sh` fetches that revision, applies the patch
  and builds it reproducibly.

## Wiring

FT2232D channel A is wired to the Ci20 J58 header as follows:

| FT2232D | Signal | Ci20 J58 |
|---------|--------|----------|
| ADBUS0  | TCK    | pin 9    |
| ADBUS1  | TDI    | pin 3    |
| ADBUS2  | TDO    | pin 5    |
| ADBUS3  | TMS    | pin 7    |
| ADBUS4  | TRST_N | pin 1    |
| GND     | GND    | pin 2, 4, 6, 8, 10 or 12 |

J58 pin 14 is the Ci20 3.3 V reference.  Do not connect it to a 5 V FTDI
I/O supply.  FT2232D channel B is connected to the Ci20 UART at 3.3 V logic
levels with TX and RX crossed and a common ground.

## Dependencies and build

On macOS with Homebrew:

```sh
brew install libusb pkg-config autoconf automake libtool
make -C tools/ci20
make -C tools/ci20 openocd
```

The OpenOCD build is pinned to OpenOCD-XBurst commit
`e73c62f593dfba5631a767826cb07a641c4ee8ec`.  Its output is:

```text
tools/ci20/openocd/build/OpenOCD-XBurst/src/openocd
```

The OpenOCD-XBurst sources are GPLv2.  The stored patch is source code and is
distributed under the same license as the patched files.

## macOS FTDI ownership

Apple's DriverKit FTDI driver must not own the FT2232D while the raw libusb
tools are running.  Disable its matching service for the current boot:

```sh
sudo launchctl bootout system/com.apple.DriverKit.AppleUSBFTDI-0x10000224c
```

Start UART in one terminal.  `Ctrl-]` exits:

```sh
sudo tools/ci20/ci20-ftdi-uart
```

The program deliberately claims only channel B and does not reset the FTDI,
change its USB configuration, purge channel A, or detach another interface.
OpenOCD can therefore use channel A concurrently.

## Normal UART and JTAG workflow

Keep the UART reader running in the first terminal for the entire test:

```sh
sudo tools/ci20/ci20-ftdi-uart
```

In a second terminal, start a persistent OpenOCD server. Binding the debug
ports to loopback is important because OpenOCD does not authenticate clients:

```sh
OCD=tools/ci20/openocd/build/OpenOCD-XBurst/src/openocd
CFG=tools/ci20/openocd/ci20-xburst.cfg

sudo "$OCD" -d2 -s tools/ci20/openocd/build/OpenOCD-XBurst/tcl \
  -f "$CFG" \
  -c "bindto 127.0.0.1; init; irscan jz4780.cpu 0x14; jz4780.cpu arp_examine"
```

There is deliberately no `shutdown` command in this invocation. OpenOCD keeps
FT2232D channel A and exposes its usual local services: GDB on port 3333,
interactive telnet on port 4444, and Tcl RPC on port 6666. The recommended
interactive diagnostic path is telnet:

```sh
telnet 127.0.0.1 4444
```

At the OpenOCD prompt, non-destructive status commands are:

```text
targets
poll
ci20_core_probe
ci20_ecr_update_probe
```

Do not launch a second OpenOCD while this server owns channel A. Run one-shot
procedures through port 4444 instead. Type `shutdown` at the OpenOCD prompt
when the debug session is finished, or use the telnet escape to disconnect
while leaving the server running.

## Snapshot a hung ReBSD target

Start the persistent server before the workload so it is already available if
both UART and USB stop responding. Leave the UART reader open, reproduce the
failure, connect to port 4444, and run:

```text
ci20_halt_snapshot
```

The procedure selects JZ4780 core 0, requests a halt, waits at most two
seconds, prints PC (`pc`), stack pointer (`r29`) and return address (`r31`),
and resumes the target even if a register read fails. A successful result ends
with output equivalent to:

```text
ci20 halt snapshot: rc=0 result= final=running
```

Confirm in the UART terminal that ReBSD accepts input after the snapshot. Save
the complete OpenOCD and UART output, the tested image SHA-256, and the workload
which triggered the stop. If halt/resume alone restores UART, USB, and timer
activity, treat that as evidence for the Ci20 idle/`WAIT` investigation in
`docs/CI20_USB.md`; it is not proof of an EHCI failure.

Prefer `ci20_halt_snapshot` to manual `halt`, `reg`, and `resume` commands. A
manual sequence can leave the board halted if a later command or connection
fails. If the snapshot reports a nonzero `rc` or does not end in `running`,
power-cycle only after saving the OpenOCD output and checking JTAG wiring,
target power, and the 100 kHz adapter setting.

## JTAG probes

Set a short alias for the built binary and configuration:

```sh
OCD=tools/ci20/openocd/build/OpenOCD-XBurst/src/openocd
CFG=tools/ci20/openocd/ci20-xburst.cfg
```

Scan and inspect both JZ4780 core selectors without halting the CPU:

```sh
sudo "$OCD" -d2 -s tools/ci20/openocd/build/OpenOCD-XBurst/tcl \
  -f "$CFG" -c "init; ci20_core_probe; shutdown"
```

Verify the XBurst ECR Update-DR path without setting `EjtagBrk`:

```sh
sudo "$OCD" -d2 -s tools/ci20/openocd/build/OpenOCD-XBurst/tcl \
  -f "$CFG" -c "init; ci20_ecr_update_probe; shutdown"
```

A working result changes the middle value by clearing `ProbTrap`, for example:

```text
ci20 ECR update: before=4008C000 cleared=40088000 restored=4008C000
```

Briefly halt core 0, print PC/SP/RA and always attempt to resume it:

```sh
sudo "$OCD" -d2 -s tools/ci20/openocd/build/OpenOCD-XBurst/tcl \
  -f "$CFG" \
  -c "init; irscan jz4780.cpu 0x14; jz4780.cpu arp_examine; ci20_halt_snapshot; shutdown"
```

Keep the UART terminal open and verify that U-Boot or ReBSD still responds
after a halt snapshot.

If an otherwise silent ReBSD system resumes UART and USB operation only after
this halt/resume cycle, record the snapshot as evidence of an idle wakeup
failure; do not treat JTAG as a production recovery mechanism. Ci20 currently
avoids the JZ4780/XBurst1 `WAIT` instruction in the scheduler idle path for
this reason. The workaround, its power tradeoff, and the gates required before
restoring low-power `WAIT` are documented in `docs/CI20_USB.md`.

## Why the OpenOCD patch is required

The patch contains four Ci20/XBurst correctness fixes:

1. FTDI scans ending in Idle go directly through `Update-DR` instead of
   `Pause-DR`.  XBurst treats `Pause-DR` as another capture while ECR is
   selected, otherwise discarding the value just shifted in.
2. The cached ECR template does not replay the XBurst read/write `ROCC` bit.
3. A failed `EjtagBrk` or PRACC operation is propagated instead of continuing
   with an invalid register context.
4. The first XBurst PRACC sequence disables and invalidates the branch target
   buffer through CP0 Config7 before normal context access.

Do not replace this build with stock Homebrew OpenOCD's generic `mips_m4k`
target: it can report a false `pc=0` halt and does not implement the JZ4780
XBurst core-selection and PRACC behavior.
