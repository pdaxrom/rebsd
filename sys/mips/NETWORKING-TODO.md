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

## Phase 2: User ABI and libc

- [ ] Verify all socket syscall numbers in `include/syscall.h` match the
  kernel syscall table.
- [ ] Add missing MIPS libc syscall wrappers if any generated wrapper is not
  built or installed.
- [ ] Audit public headers used by native `cc`/`pcc`; `unistd.h` still has
  prototypes that older PCC syntax rejects.
- [x] Install public socket/network headers into the target `/usr/include`
  tree through the existing generated header flow.
- [x] Stage a tiny target-side socket ABI smoke first:
  - `socket(AF_INET, SOCK_DGRAM, 0)`
  - `bind(INADDR_ANY:0)`
  - `getsockname`
  - raw ICMP socket open/close
  - `socket(AF_UNIX, SOCK_STREAM, 0)`
  - `socketpair(AF_UNIX, SOCK_STREAM, 0)` plus byte read/write
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
  - TCP loopback listen/connect/read/write smoke (verified)
  - route table smoke (`route add/delete` verified)
  - netstat smoke for `-i`, `-r`, `-s`, `-m`, `-u`, `-p tcp`
    output diagnostics

## Phase 4: TCP and Extended Coverage

- [x] Enable TCP files once ICMP/UDP loopback are stable.
- [x] Add simple TCP loopback smoke:
  - listen/connect on `127.0.0.1`
  - send/receive a short payload
  - close client, accepted socket, and listener cleanly
- [ ] Add TCP reuse/TIME_WAIT coverage once the basic loopback test remains
  stable:
  - close and reuse port after timeout
  - show active/closing TCP entries with `netstat`
- [ ] Review memory pressure from mbufs on Malta before carrying the same
  defaults to N64.
- [ ] Review historical 2.11BSD network families not enabled in the current
  MIPS first pass, including AF_NS/Xerox NS and AF_IMPLINK/IMP, before deciding
  whether to port their kernel sources and `netstat` decoders.

## Phase 5: N64 Enablement

- [ ] Keep N64 `INET` disabled until all Malta tests pass repeatedly.
- [ ] After Malta is stable, enable the shared MIPS network stack for N64.
- [ ] Start N64 with loopback only; no cartridge/network hardware driver yet.
- [ ] Hardware-smoke on N64:
  - `ifconfig lo0`
  - `ping 127.0.0.1`
  - UDP loopback smoke
  - TCP loopback smoke
- [ ] Only after loopback works on N64, design real network-device support.

## Deferred Items For First Pass

- No N64 hardware networking driver.
- No SLIP/PPP until loopback and core socket behavior are stable.
- No changes to the N64 config while Malta is failing.
- No rewriting userland tools before the kernel ABI is proven.
