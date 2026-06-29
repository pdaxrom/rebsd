# N64 USB Network Bridge

`n64usbnet-bridge` is the host-side companion for the N64 `usbn0` driver.
It talks to the n64cart USB device with the RetroBSD vendor-specific bulk
framing and forwards Ethernet frames to a host TAP device.

Build:

```
make -C tools/n64usbnet
```

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

On N64:

```
/sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up
/usr/bin/ping -c 1 10.64.0.1
```

The bridge only uses USB bulk EP1 OUT and EP2 IN.  It does not send any
n64cart ROMFS/flash commands.
