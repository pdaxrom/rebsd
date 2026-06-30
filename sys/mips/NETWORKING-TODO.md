# RetroBSD MIPS Networking TODO

This tracks the first networking branch.  Bring the stack up on Malta first;
do not enable or test it on N64 until the Malta path is stable.

Source reference:

- Original 2.11BSD tree: `/Users/sash/Work/tmp/2.11BSD`
- Kernel networking sources:
  - `usr/sys/h`: socket, mbuf, domain, protocol headers
  - `usr/sys/sys`: `uipc_*`, `sys_socket.c`, socket syscall support
  - `usr/sys/net`: interface, route, raw socket, loopback sources
  - `usr/sys/netinet`: IPv4, ICMP, UDP, TCP sources
- Userland network tools:
  - `usr/sbin/ifconfig`
  - `usr/sbin/route`
  - `usr/bin/ping`
  - `usr/ucb/netstat`

## Phase 1: Malta INET/Loopback Kernel

- [x] Add an `INET` build option/service for Malta only.
- [x] Import the required kernel headers:
  - `sys/include/socket.h`
  - `sys/include/socketvar.h`
  - `sys/include/mbuf.h`
  - `sys/include/domain.h`
  - `sys/include/protosw.h`
  - `sys/net/*.h`
  - `sys/netinet/*.h`
- [x] Add a shared MIPS `net_mac.h` compatibility shim for the original
  PDP overlay macros.  MIPS must call socket/network functions directly.
- [x] Import and adapt socket/uipc sources:
  - `uipc_syscalls.c`
  - `sys_socket.c`
  - `uipc_domain.c`
  - `uipc_mbuf.c`
  - `uipc_proto.c`
  - `uipc_socket.c`
  - `uipc_socket2.c`
  - `uipc_usrreq.c` imported and enabled on Malta as `service unixdomain`;
    N64 still stays disabled until the Malta path is stable.
- [x] Import the minimal interface and route layer:
  - `af.c`
  - `if.c`
  - `if_loop.c`
  - `raw_cb.c`
  - `raw_usrreq.c`
  - `route.c`
- [x] Import the IPv4 protocol layer needed for loopback:
  - `in.c`
  - `in_pcb.c`
  - `in_proto.c`
  - `ip_icmp.c`
  - `ip_input.c`
  - `ip_output.c`
  - `raw_ip.c`
  - `udp_usrreq.c`
  - `tcp_debug.c`
  - `tcp_input.c`
  - `tcp_output.c`
  - `tcp_subr.c`
  - `tcp_timer.c`
  - `tcp_usrreq.c`
- [x] Add a portable MIPS `in_cksum()` implementation.
- [x] Add a MIPS `netinit()` path that initializes mbufs, interfaces,
  loopback, and domains without PDP/UNIBUS code.
- [x] Build Malta kernel with `INET`.
- [x] Boot Malta kernel with `INET` in QEMU.
- [x] Enable AF_UNIX on Malta and verify `socketpair(AF_UNIX, SOCK_STREAM)`.
- [x] Verify AF_UNIX pathname stream sockets on Malta:
  - bind/listen on `/tmp/net-smoke-unix.$$`
  - connect/accept
  - byte read/write
  - close and unlink the socket path
- [x] Verify basic socket `select`/`ioctl` behavior on Malta:
  - zero-timeout read `select` on an empty AF_UNIX socket
  - write readiness through `select`
  - read readiness after a byte write
  - `FIONREAD` before and after read
  - `FIONBIO` empty read returns `EWOULDBLOCK`

## Phase 2: User ABI and libc

- [x] Verify socket syscall numbers in `include/syscall.h` match the kernel
  syscall table for the enabled socket/select/ioctl calls.
- [x] Verify MIPS libc syscall wrappers are generated, archived, and reachable
  for the current smoke coverage:
  - `accept`, `bind`, `connect`, `getpeername`, `getsockname`,
    `getsockopt`, `ioctl`, `listen`, `recv`, `recvfrom`, `send`, `sendto`,
    `select`, `setsockopt`, `shutdown`, `socketpair`
- [x] Audit public headers used by native `cc`/`pcc`; verified on Malta with
  `/root/net-header-smoke.sh`:
  - `sys/types.h`
  - `sys/time.h`
  - `sys/select.h`
  - `sys/socket.h`
  - `sys/ioctl.h`
  - `sys/un.h`
  - `netinet/in.h`
  - `unistd.h`
- [x] Install public socket/network headers into the target `/usr/include`
  tree through the existing generated header flow.
- [x] Stage a tiny target-side socket ABI smoke first:
  - `socket(AF_INET, SOCK_DGRAM, 0)`
  - `bind(INADDR_ANY:0)`
  - `getsockname`
  - raw ICMP socket open/close
  - `socket(AF_UNIX, SOCK_STREAM, 0)`
  - `socketpair(AF_UNIX, SOCK_STREAM, 0)` plus byte read/write
  - `bind`/`listen`/`connect`/`accept` with an AF_UNIX pathname socket
- [x] Extend target-side socket smoke after QEMU boots:
  - `bind(127.0.0.1:port)`
  - `sendto`/`recvfrom` over `lo0`
  - clean close/error handling

## Phase 3: Malta Network Tools

- [x] Port and stage `ifconfig`; verified on Malta:
  - `ifconfig lo0`
  - `ifconfig lo0 inet 127.0.0.1 up`
- [x] Port and stage `route`; verified on Malta with:
  - `route add host 127.0.0.2 127.0.0.1 0`
  - `route delete host 127.0.0.2 127.0.0.1`
- [x] Port and stage `ping`; verified on Malta with:
  - `ping -c 1 127.0.0.1`
- [x] Port and stage `netstat` first pass for the enabled MIPS families:
  - `netstat -i` interfaces (verified on Malta)
  - `netstat -r` routes (verified on Malta)
  - `netstat -s` protocol stats (verified on Malta)
  - `netstat -m` mbuf stats (verified on Malta)
  - `netstat -u` AF_UNIX table scan (verified on Malta with no active sockets)
- [x] Add `/root/net-smoke.sh` for Malta:
  - show interfaces (`ifconfig lo0` verified)
  - configure `lo0 127.0.0.1` (verified)
  - ping `127.0.0.1` (verified)
  - UDP loopback smoke (verified)
  - TCP loopback listen/connect/send/recv smoke (verified)
  - route table smoke (`route add/delete` verified)
  - `getpeername` and `getsockopt(SO_TYPE)` smoke
  - `shutdown` smoke
  - netstat smoke for `-i`, `-r`, `-s`, `-m`, `-u`, `-p tcp`
    output diagnostics

## Phase 4: TCP and Extended Coverage

- [x] Enable TCP files once ICMP/UDP loopback are stable.
- [x] Add simple TCP loopback smoke:
  - listen/connect on `127.0.0.1`
  - send/receive a short payload
  - close client, accepted socket, and listener cleanly
- [x] Add first TCP reuse/netstat coverage:
  - run `netstat -a -f inet` while the TCP loopback connection is active
  - close and bind/listen the same local port again with `SO_REUSEADDR`
- [x] Add TCP TIME_WAIT coverage once the first reuse test remains stable:
  - show closing/TIME_WAIT TCP entries with `netstat`; verified by
    `/root/net-smoke.sh` on Malta.
- [x] Extend `select`/`ioctl` smoke to TCP sockets:
  - blocking read readiness through `select`
  - `FIONREAD` before TCP stream read
  - write readiness through `select`
  - `FIONBIO` empty read returns `EWOULDBLOCK`
- [x] Review memory pressure from mbufs on Malta before carrying the same
  defaults to N64; `/root/net-smoke.sh` now verifies `netstat -m` reports no
  mbuf allocation drops, waits, or protocol drain calls after the loopback
  socket smoke.
- [x] Review historical 2.11BSD network families not enabled in the current
  MIPS first pass:
  - the original 2.11BSD tree has Xerox NS sources under `usr/sys/netns`
    and IMP sources under `usr/sys/netimp`.
  - the current MIPS import keeps public `AF_NS` and `AF_IMPLINK` constants
    in `sys/include/socket.h`, but does not import/register `nsdomain` or
    `impdomain` for Malta.
  - current MIPS `netstat` intentionally walks the enabled `AF_INET` and
    `AF_UNIX` tables only; NS/IMP table decoders need a separate import.
  - do not silently enable either family in this first pass.  Treat NS/IMP
    as a separate porting decision after loopback INET/UNIX remains stable.

## Phase 5: N64 Enablement

- [x] Keep N64 `INET` disabled until all Malta tests pass repeatedly.
- [x] After Malta is stable, enable the shared MIPS network stack for N64.
- [x] Start N64 with loopback only; no cartridge/network hardware driver yet.
- [x] Hardware-smoke on N64:
  - `ifconfig lo0`
  - `ping 127.0.0.1`
  - UDP loopback smoke
  - TCP loopback smoke
- [ ] Design real network-device support now that N64 loopback works:
  - [x] first bring up a QEMU virtual NIC on Malta and debug the generic driver
    path there:
    - `make -C sys/mips/malta run-net`
    - `ifconfig ne0 inet 10.0.2.15 netmask 255.255.255.0 up`
    - `route add default 10.0.2.2 1`
    - `ping -c 1 10.0.2.2`
    - `/root/ne2k-smoke.sh` covers gateway ICMP, route/interface counters,
      and TCP echo over QEMU `guestfwd` to host `/bin/cat`.
  - [x] after the Malta virtual NIC path works, design the N64 hardware
    backend as a USB network adapter path.
  - [ ] N64 cartridge USB Ethernet gadget:
    - [x] Confirmed the existing N64cart USB example is a device/gadget
      implementation, not USB host support; first hardware network path will
      expose N64 as a USB Ethernet-like device to a host bridge.
    - [x] Keep the first protocol vendor-specific bulk USB over EP1 OUT/EP2 IN,
      matching the existing N64cart USB device plumbing; defer CDC ECM/RNDIS
      until the basic data path is stable.
    - [x] Use a small Ethernet-frame framing header plus 64-byte USB bulk
      fragmentation/reassembly.  Host tests must pass before touching N64
      hardware.
    - [x] Split implementation into a reusable framing layer, an N64cart USB
      device-controller layer, and an `if_usbn` Ethernet interface upper half.
    - [x] Add a Malta fake transport for `if_usbn` so ARP/IP/ICMP can be
      validated without N64 USB hardware.
    - [x] Test the `if_usbn` upper half on Malta/fake transport before enabling
      the N64 USB controller backend.
    - [x] Add the N64cart USB device-controller backend.
    - [x] Move the N64 backend from CP0 timer polling to CART USB IRQ/IP3.
    - [ ] USB networking must not touch flash erase/write/read mode transitions;
      ROMFS/cartflash locking stays separate from USB packet I/O.
    - [x] Add the host-side bridge that exposes the vendor-specific bulk USB
      framing as a normal host TAP/TUN or socket-backed Ethernet endpoint.
      Current first pass is `tools/n64usbnet/n64usbnet-bridge`, a libusb to
      TAP bridge.
    - [x] Test `usbn0` on real N64 hardware with the host bridge:
      - USB enumeration
      - `ifconfig usbn0 inet ... up`
      - ARP exchange
      - ICMP ping across the USB link
      - physical USB cable unplug/replug with re-enumeration
    - [ ] Add a TCP smoke across the USB link.
    - [ ] Replace the vendor-specific bridge protocol with a standard USB
      Ethernet gadget class.  CDC ECM should be first because Linux and macOS
      bind it without a userspace bridge; RNDIS can be added later if Windows
      is a target.

## Deferred Items For First Pass

- Do not treat the N64 USB network path as hardware-verified until the
  host-side bridge and real N64 smoke pass.
- No SLIP/PPP until loopback and core socket behavior are stable.
- No changes to the N64 config while Malta is failing.
- No rewriting userland tools before the kernel ABI is proven.
