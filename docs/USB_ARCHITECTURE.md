# ReBSD USB Host Architecture

This document defines the ownership and state rules for the compact USB host
core and its first OHCI attachment. The historical object model follows
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
UART-to-`ttyinput()` interrupt model. Descriptor work and driver attachment
remain in boot process context. A later general USB completion queue is still
required before bulk I/O, hub exploration, or callbacks that may sleep.

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
submit/abort, root-hub control, and poll operations.  Polling and interrupt
completion both end by calling `usb_xfer_complete()`; the generic core has no
separate polling-only completion path.

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
