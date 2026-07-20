# N64cart USB GDB stub

The N64 debug kernel can dedicate the cartridge USB controller to a GDB
Remote Serial Protocol endpoint. In this mode the cartridge enumerates as
the CDC ACM device `N64cart GDB`; the normal cartridge UART and `/dev/ttyS0`
remain unchanged. USB Ethernet is deliberately absent because the same USB
device controller, endpoints, and CART/IP3 interrupt are owned by the
debugger.

Build a matching ROM and symbol file out of tree:

```sh
make -C sys/mips BOARD=n64 O=/tmp/rebsd-n64-gdb reconfig
make -C sys/mips BOARD=n64 O=/tmp/rebsd-n64-gdb \
    N64_USB_GDB=1 N64_MINIMAL_ROOTFS=1 \
    N64_MINIMAL_PCC_SMOKE=1 \
    N64_MINIMAL_ROOTFS_KBYTES=6144 kernel.z64
```

The useful outputs are under
`/tmp/rebsd-n64-gdb/obj/sys/mips/n64/`: flash `kernel.z64` and retain
`unix.elf` for symbols. Changing `N64_USB_GDB` changes the build-mode stamp,
so a normal and a debug kernel should normally use different `O=` trees.
The debug build keeps DWARF source/line information in `unix.elf`; the ROM
embeds a separate `unix.runtime.elf` without DWARF so flashing stays quick.

The command above uses the existing 6 MiB PCC hardware-test rootfs. It includes
the native compiler, its runtime and headers, and `/root/pcc-smoke-all.sh`, but
still omits unrelated full-userland content. The runner prints every smoke and
compiler phase so the last line before a hang can be correlated with GDB.

Linux normally exposes the endpoint as `/dev/ttyACM0`; macOS uses a
`/dev/cu.usbmodem*` device. The configured baud rate has no hardware meaning.
Connect directly, without the USB-network bridge:

```text
$ mips64-elf-gdb /tmp/rebsd-n64-gdb/obj/sys/mips/n64/unix.elf
(gdb) set architecture mips:4300
(gdb) target remote /dev/ttyACM0
```

Opening the RSP session stops the running CPU through CART/IP3. While
connected, GDB Ctrl-C uses the same interrupt for asynchronous break-in.
`continue`, `step`, software breakpoints, register access, and direct-mapped
kernel memory access are implemented. The stub reports one CPU/thread.
IP3 is restored on every user-mode entry and remains unmasked while ordinary
kernel `splhigh()` sections mask the MI and timer sources, so break-in does not
disappear after starting a process and can stop VM/exec critical sections.

## Crash-safety boundary

The exception entry switches USB break-in and `break` exceptions to a static
8 KiB emergency stack before calling C. The RSP packet buffers and breakpoint
table are static. The stopped path does not call the VM system, scheduler,
tty layer, network stack, allocator, or `printf`; controller access is through
KSEG1. Failed kernel TLB faults and other otherwise-fatal kernel exceptions
also enter an already-connected debugger before continuing to `panic`.

This means the debugger remains usable when page tables, the active
`vmspace`, or higher kernel services are damaged, provided that RDRAM, the
exception vectors, the stub itself, and the cartridge PI/USB registers remain
accessible. It cannot recover from destroyed exception vectors, corrupted
stub code/static storage, a wedged PI bus, or lost RDRAM.

## RESET crash dump

`N64_RESET_DUMP` defaults to the value of `N64_USB_GDB`. The physical RESET
button is an independent last-resort stop when USB/GDB cannot interrupt a
wedged kernel. The button's pre-NMI interrupt saves the complete exception
frame, the current process identity, selected kernel pointers, and 24 words
from a direct-mapped stack into the N64 warm-reset retention area. It remains
enabled through ordinary `splhigh()` critical sections.

The console hardware still performs its unavoidable warm NMI about half a
second after the first press. Early in that warm boot the kernel detects the
retained record, switches to the 320x240 panic console, prints PC, RA, SP,
Cause, Status, BadVAddr, PID, process name, and selected state, then stops.
It does not continue into normal startup. Press RESET a second time to clear
the record; the following warm boot then proceeds normally.

Stage0 repeats the PIF boot-complete handshake on every cold or warm entry.
This releases the previous NMI state and rearms the physical RESET button;
omitting the warm-boot handshake makes the button work only once per power-on.
The ROM uses libdragon's production IPL3, whose reset type is passed in SP
DMEM rather than the legacy `0x8000030c` word. Stage0 copies that bootinfo
field before SP DMEM is reused; reading the untouched legacy word directly
would make dump detection depend on stale RDRAM contents.

The retained record has three states. During normal execution, the timer
updates a small `live` snapshot at most once per second without console
tracing. A delivered pre-NMI replaces it with an exact frame. Rate limiting
the fallback is important because publishing the uncached retained record on
every TLB exception would severely slow memory-intensive user processes. If
pre-NMI is masked by EXL/ERL or IE, the warm boot displays the last sampled
snapshot instead of silently rebooting. Displaying either kind marks the
record as `displayed`. The next RESET clears that state and boots normally
even when its own pre-NMI was missed, so successive presses alternate dump,
boot, dump, boot.

Keep the `unix.elf` from the exact same build. The hexadecimal `pc` and `ra`
shown on the death screen can be resolved with GDB, for example:

```text
(gdb) info symbol 0x80012345
(gdb) list *0x80012345
```

An exact capture can still fail if RESET arrives while the CPU has EXL/ERL
set, or execution is stuck in a hardware transaction until the real NMI. The
retained live snapshot covers those cases. A screen is impossible only if the
exception vectors, the retained 64-byte NMI buffer, or RDRAM itself have been
destroyed.

For the same reason, `m`/`M` currently accept only RDRAM addresses in KSEG0 or
KSEG1. TLB-mapped user addresses are rejected with `E14` instead of risking a
nested debugger fault. FPU registers are reported as zero because lazy FPU
state is not copied onto the emergency frame. Hardware watchpoints and
multi-CPU/thread control are not yet exposed. There are 16 software
breakpoint slots; stepping covers the normal MIPS III branch/jump and delay
slot cases, with both outcomes armed for an FPU condition branch. A software
breakpoint on a branch whose resolved next PC is the same address cannot be
single-stepped without displaced-instruction support; that case is not yet
implemented.
