# N64 USB Network Bridge

`n64usbnet-bridge` is the host-side companion for the N64 `usbn0` driver.
It talks to the n64cart USB device with the RetroBSD vendor-specific bulk
framing and forwards Ethernet frames to a host TAP device.

The default N64 kernel now uses CDC ECM instead.  This bridge is only needed
when `sys/mips/n64/Config` selects `options "USBNET_VENDOR"` instead of
`options "USBNET_ECM"`.

Build:

```
make -C tools/n64usbnet
```

List devices visible to libusb:

```
tools/n64usbnet/n64usbnet-bridge --list
```

The N64 USBNet device should appear as `1209:6800`.  If it does not appear in
this list, the host has not enumerated the n64cart USB device yet; check that
the N64 is running a kernel with `usbn0` support and that the USB cable is
connected to the n64cart USB device port.

Linux TAP example:

```
sudo ip tuntap add dev tap-retrobsd mode tap
sudo ip addr add 10.64.0.1/24 dev tap-retrobsd
sudo ip link set tap-retrobsd up
sudo tools/n64usbnet/n64usbnet-bridge --tap tap-retrobsd
```

BSD/macOS TAP example, when a TAP driver is installed:

```
sudo ifconfig tap0 10.64.0.1/24 up
sudo tools/n64usbnet/n64usbnet-bridge --tap /dev/tap0
```

macOS built-in `utun` example, without a TAP driver:

```
sudo tools/n64usbnet/n64usbnet-bridge --utun
```

The bridge opens a fresh `utunN` interface and configures it automatically when
run as root.  It prints the allocated interface and the active point-to-point
addresses.

To skip automatic configuration, pass `--no-config`.  In that mode the bridge
prints the exact `ifconfig` command to run in another terminal, for example:

```
sudo ifconfig utun7 inet 10.64.0.1 10.64.0.2 up
```

`utun` is an IPv4 point-to-point interface, not Ethernet.  The bridge keeps the
N64 side as Ethernet, answers ARP for the host address, strips Ethernet headers
before writing IPv4 packets to `utun`, and wraps `utun` IPv4 packets back into
Ethernet frames for `usbn0`.

Use `-v` while bringing the link up.  A working ping should show ARP plus IP
traffic in both directions:

```
utun arp reply
usb -> utun ip proto=1 10.64.0.2 -> 10.64.0.1 len=84
utun -> usb ip proto=1 10.64.0.1 -> 10.64.0.2 len=84
host -> usb 98 bytes
```

On N64:

```
/sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up
/usr/bin/ping -c 1 10.64.0.1
```

For TCP smoke testing over CDC ECM or the vendor-specific bridge, use the
included echo server.  Plain `nc -lk` is not an echo server on macOS; it prints
received data to the terminal and does not send it back on the same connection.

Host:

```
tools/n64usbnet/n64usbnet-echo 10.64.0.1 2323
```

N64:

```
/root/usbn-tcp-smoke.sh
```

This is a host-link configuration.  Reaching external hosts through `utun`
also needs a default route and resolver on RetroBSD plus host-side forwarding
and NAT on macOS.  The bridge does not install PF rules or provide a DNS
forwarder yet.

The bridge only uses USB bulk EP1 OUT and EP2 IN.  It does not send any
n64cart ROMFS/flash commands.
