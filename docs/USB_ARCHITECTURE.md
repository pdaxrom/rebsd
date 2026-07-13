# ReBSD USB Host Architecture

This document defines the ownership and state rules for the compact USB host
core and its OHCI/EHCI attachments. The historical object model follows
NetBSD 3.1, while allocation, attachment, and synchronization use native ReBSD
rules.

## Layer Boundary

The dependency direction is:

```text
Ci20 platform attachment
        -> generic OHCI or EHCI HCD
        -> USB core and transfer API
        -> hub and interface matching
        -> HID or mass-storage class driver
```

The HCD sees USB pipes and transfers.  It does not select class drivers.  The
USB core sees only `struct usb_hcd_ops`; it does not include Ci20 headers or
access controller registers.  Class drivers see interfaces, endpoints, pipes,
and transfers; they do not distinguish OHCI from EHCI.

## Bounded Ownership

`struct usb_core` owns the machine-independent pools.  Their initial default
limits are:

| Object | Limit |
| --- | ---: |
| devices | 8 |
| interfaces | 16 |
| non-default endpoints | 32 |
| pipes | 16 |
| transfers | 16 |
| registered interface drivers | 8 |
| deferred USB tasks | 8 |
| root-hub ports per HCD | 8 |

Each object has one `used` bit and is returned to its pool only after all HCD
references have ended.  Configuration bytes are stored in the device's fixed
1024-byte buffer.  Descriptor parsing uses one scratch result owned by the
core, so enumeration is deliberately serialized.  There is no kernel heap,
hidden fallback allocation, or per-device thread.

Ownership relationships are:

- a bus belongs to its platform/HCD attachment and points to one core;
- a device belongs to exactly one bus and owns its default endpoint;
- interfaces and non-default endpoints come from core-wide pools and belong
  to one device;
- a pipe holds one endpoint reference and belongs to one device;
- a transfer belongs to its allocating device and may reference one open
  pipe;
- an HCD may reference an active transfer only between successful submission
  and completion or abort;
- a matched class driver owns `ui_private` from successful attach until its
  detach callback.

## Enumeration Transaction

`usb_device_enumerate()` is a transaction with one cleanup path:

1. allocate a device at address zero and open its default control pipe;
2. read the first eight bytes of the device descriptor;
3. validate endpoint-zero packet size;
4. reserve the lowest free address in the range 1 through 127;
5. send `SET_ADDRESS` and only then publish the address in the device;
6. read and validate the full device descriptor;
7. read the configuration header, bound `wTotalLength`, then read the full
   configuration into the device buffer;
8. parse and instantiate interfaces and endpoints from fixed pools;
9. send `SET_CONFIGURATION`;
10. select the highest positive interface-driver match and attach it.

Any error closes the default pipe, releases all created objects, releases the
USB address, and leaves no partially published device.  An unmatched interface
is valid and remains unclaimed.  A selected driver's attach failure aborts the
whole enumeration transaction.

## Transfer State

A transfer moves through these states:

```text
FREE -> ALLOCATED -> ACTIVE -> COMPLETED -> FREE
                         \-> CANCELLED  -> FREE
                         \-> TIMED_OUT  -> FREE
                         \-> DISCONNECTED -> FREE
```

`usb_xfer_complete()` accepts only an active transfer.  Therefore normal
completion, explicit cancellation, timeout, and disconnect have one winner
and exactly one callback.  A later completion attempt is ignored.  A transfer
cannot be freed while active.

The current synchronous path is polling-based.  After submission it calls the
same HCD `poll` method that early OHCI will use, optionally invoking a
platform-provided one-millisecond delay callback.  On expiration the core asks
the HCD to remove the transfer, then publishes `USB_STATUS_TIMEOUT`.  The HCD
must not call a second completion from its abort method.

The first periodic slice completes one HID interrupt-IN transfer directly
from the Ci20 OHCI interrupt. Its callback is bounded to an eight-byte boot
report, console character submission, and TD rearm, matching the existing
UART-to-`ttyinput()` interrupt model. Callbacks that may sleep remain
unsupported until general transfer completion is moved to deferred context.

Root-port changes follow a different path. The OHCI interrupt acknowledges
RHSC, masks further RHSC delivery, and schedules one coalescing root-hub task.
ReBSD proc0 drains the bounded task queue from the existing swapper scheduler
loop, outside hardware interrupt context. It performs debounce delays, port
reset, descriptor traffic, enumeration, driver attach, and driver detach,
then reenables RHSC. A pending RHSC status is deliberately not cleared while
reenabling, so a change that arrived during exploration schedules another
pass instead of being lost.
A periodic WDH error can precede the root-port status change on real hardware.
The error path therefore masks WDH, RHSC, and the OHCI master interrupt, then
queues the same deferred root-hub probe directly. The proc0 runner inspects the port
and reenables RHSC/MIE. A later RHSC remains pending while masked and is
delivered after the probe, so neither a timing window nor an IRQ spin is
required.

No USB process is created. ReBSD `newproc()` constructs a swappable user
process image and is not a kernel-thread primitive; hardware tests showed
that using it for a permanent SSYS child left a stale entry in the hashed
sleep queue and caused `panic: wakeup`. Scheduling a task wakes proc0 on both
of its native scheduler wait channels. Proc0 checks the USB queue at the top
of its loop and atomically rechecks it before either scheduler sleep, closing
the lost-wakeup window.

## Root-Hub Exploration

`struct usb_root_hub` is machine-independent and owns one fixed port record
per reported HCD root port. Startup validates the root-port operations, powers
all ports, performs one synchronous initial exploration, and enables root-hub
change interrupts. Later exploration runs only through the proc0 task runner.

For each port, exploration reads and clears bounded change bits. A connection
or enable change first disconnects any existing child, even if the current
line state already shows a replacement device. If a device is currently
connected, exploration waits 100 ms for debounce/power stabilization, reads
status again, resets and enables the port, detects low/full/high speed, and
calls the common enumeration transaction. Port status, reset, and enumeration
errors are reported through a platform callback and never hidden as success.

The current compact hub code implements OHCI and EHCI root-port attach/detach.
External hub interrupt endpoints and downstream-port control are a later
phase; class drivers and the USB core do not depend on that later policy.

## Disconnect Order

Disconnect is idempotent and proceeds in this order:

1. mark the device disconnected so no new transfer can start;
2. abort active transfers with `USB_STATUS_DISCONNECTED`;
3. call attached interface drivers' detach callbacks in reverse order;
4. close every remaining pipe, including endpoint zero;
5. invalidate remaining completed transfer references to the device;
6. release endpoints and interfaces;
7. release the USB address and device slot.

Driver detach callbacks run while descriptors and interface objects are still
valid, but after active I/O has reached a terminal state.

## HCD Contract

`struct usb_hcd_ops` contains start/stop, pipe open/close, transfer
submit/abort, root-hub control, poll, root-port count/status/power/reset/change
acknowledgement, and root-change interrupt enable operations. Polling and
interrupt completion both end by calling `usb_xfer_complete()`; the generic
core has no separate polling-only completion path. The HCD reports a root
change through a callback and never enumerates a device itself.

The mock HCD implements the same contract as OHCI. It supplies a
root-port device descriptor and a configuration containing one HID boot
keyboard interface.  It can hold transfers, inject one control error, and
simulate disconnect.  Its tests are the gate for address reuse, driver
matching, cancellation, timeout, malformed configuration cleanup, and
disconnect/reconnect independently of hardware.

The OHCI implementation uses the same HCD contract and completion path. Its
bounded 4096-byte slab contains independent control and periodic interrupt-IN
ED/TD/buffer regions, so one persistent keyboard transfer can coexist with a
synchronous control request. One interrupt pipe is supported at a time; bulk
and general multi-pipe periodic scheduling remain later work.

The compact EHCI implementation also uses the common contract. It allocates
one contiguous, coherent 16 KiB DMA slab containing a terminated 1024-entry
periodic frame list, a circular asynchronous head, fixed aligned pipe QHs and
qTDs, setup storage, and an 8 KiB data bounce area. The initial bounded policy
allows one active high-speed control or bulk transfer at a time. Completion
may come from polling or IRQ 20, and `usb_bulk_transfer()` applies the common
poll/timeout/abort lifecycle. Isochronous, split-transaction, and periodic
endpoint scheduling are deliberately absent.

Ci20 J23 is one physical port shared by the two controller views. EHCI claims
high-speed devices. A low-speed line indication, or a reset that does not
enable the port at high speed, sets EHCI Port Owner and hands the direct
low/full-speed device to OHCI. EHCI hides a companion-owned connection from
its own root hub while still exposing and clearing raw change bits, preventing
a stuck port-change interrupt. After OHCI reports detach, the platform layer
clears Port Owner; a connected high-speed replacement is then explored by
EHCI. This ownership callback is platform policy and contains no JZ4780
register access in the generic EHCI driver.

## Context and Future SMP Rules

- hardware interrupt context acknowledges controller status and performs only
  the currently bounded keyboard completion or queues root-hub work;
- the proc0 USB task runner owns hub exploration, enumeration, and detach work;
- calling processes own synchronous control requests and may poll or sleep;
- `splhigh` protects only queue publication/removal and other short terminal
  state transitions; descriptor I/O and delays run with interrupts enabled;
- an HCD must abort and forget an active transfer before its DMA storage or
  owning device can be released.

The implementation is currently UP. Replacing the short `splhigh` regions
with a queue lock and making object state atomic is the future SMP boundary;
the HCD/core and platform/core interfaces do not require redesign for that
change.
