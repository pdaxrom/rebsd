# Creator Ci20 HDMI framebuffer

This port uses the ReBSD DRM/KMS core to drive the JZ4780 LCDC0 scanout path
into the integrated Synopsys DesignWare HDMI block. It deliberately starts
with the conservative fixed 640x480 progressive VGA timing in DVI-compatible
RGB mode:

- 640x480 visible pixels, 800x525 total;
- 25.000 MHz actual pixel clock from the 1.2 GHz MPLL divided by 48
  (the nominal VGA clock is 25.175 MHz);
- negative horizontal and vertical sync, active-high data enable;
- 32-bit XRGB8888 scanout with a 2560-byte stride;
- framebuffer at physical `0x0f800000`, with 8 MiB reserved for scanout.

The fixed mode avoids depending on DDC/EDID during first bring-up and is
accepted by ordinary HDMI monitors and capture devices. HDMI audio is not
enabled.

## Driver architecture

Architecture-independent display code is outside the MIPS tree:

- `sys/include/drm.h` defines modes, framebuffer, plane, CRTC, encoder,
  connector and device objects, plus the dumb-framebuffer ABI;
- `sys/drm/drm.c` registers devices and validates/applies modesets;
- `sys/drm/drm_fb.c` implements the common `/dev/fbN` read/write/ioctl/mmap
  path;
- `sys/drm/dw_hdmi.c` implements the Synopsys frame-composer, PHY, video
  packetizer, sampler and main-controller sequence.

`sys/mips/ci20/video.c` is the JZ4780 backend. It contains Ci20 board
power/clock control, LCDC descriptors and timing, and the four-byte-stride
MMIO binding for the common DW-HDMI bridge. The LCDC primary plane uses
foreground 1: Linux marks foreground 0 broken on JZ4780, and requires both
OSD and alpha enable for foreground 1.

Video attaches before the normal kernel clock starts. `kconfig()` therefore
starts free-running TCU channel 3 before DRM attachment; `udelay()` must not
silently return during HDMI power, PHY-I2C or PLL waits. `clkstart()` later
starts only periodic channel 0 and preserves the early delay counter.

## Kernel interfaces

`/dev/console` is the HDMI text console and receives input from the existing
USB HID keyboard driver. UART4 is exposed independently as `/dev/ttyS0`, with
its own tty state and getty/login session. Early kernel diagnostics use UART4
until DRM is ready; normal console output is HDMI-only after that point, so
the two login sessions cannot consume each other's input or output.

`/dev/fb0` is character major 5, DRM framebuffer minor 0. It supports byte
reads/writes, uncached shared `mmap(2)`, and two userspace interfaces. The
Linux fbdev-compatible subset is declared by `<linux/fb.h>`:

```text
FBIOGET_FSCREENINFO  struct fb_fix_screeninfo
FBIOGET_VSCREENINFO  struct fb_var_screeninfo
FBIOPUT_VSCREENINFO  struct fb_var_screeninfo
FBIOPAN_DISPLAY      struct fb_var_screeninfo (zero offsets only)
FBIOBLANK            0..4
```

The fixed `0x46xx` ioctl values and structure layouts match the 32-bit Linux
MIPS fbdev UAPI. Truecolor XRGB8888 is reported with RGB offsets 16/8/0.
Framebuffer mode selection maps `FBIOPUT_VSCREENINFO` to an advertised DRM
mode; virtual screens and panning are not implemented.

ReBSD-specific mode enumeration and mapping hints remain available through:

```text
DRMFBIOC_GETINFO   struct drmfb_info
DRMFBIOC_GETMAP    struct drmfb_map
DRMFBIOC_GETMODE   struct drmfb_mode
DRMFBIOC_SETMODE   struct drmfb_mode
```

The public header is `sys/include/drm.h`. This is the same API used by the N64
VI DRM backend; platform code differs only in its modes and pixel formats.
The root filesystem includes the common `fbset` and `fbview` utilities.
`fbview` streams baseline JPEG MCU blocks directly to scanout and selects
1/2, 1/4, or 1/8 JPEG downscaling for large photos instead of allocating a
full-size RGB image.
`DRMFBIOC_*` remains a native ReBSD extension and is not Linux DRM/KMS or BSD
`wsdisplay`. The Linux compatibility surface is fbdev only.

## Full 1 GiB memory map

The Ci20 DRAM is not physically contiguous:

```text
0x00000000..0x0fffffff  256 MiB low bank
0x30000000..0x5fffffff  768 MiB high bank
```

ReBSD registers both regions independently. Two permanent 256 MiB-page TLB
entries map the high bank cached at `0xc0000000..0xefffffff`; the unused odd
half of the final entry remains invalid. Page-table pages and kernel u areas
are constrained to the low direct-mapped bank because the exception refill
path and persistent kernel-stack pointers use KSEG0.

The framebuffer is reserved at the top of the low bank. Rootfs and RAM swap
therefore remain below `0x0f800000` and never cross the physical hole.

## Build and host gates

```sh
sh sys/tests/vm/smoke-vm.sh test
make -C sys/mips BOARD=ci20 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc all
```

The VM suite builds and validates both the legacy 256 MiB map and the full
1 GiB two-bank map.

## Hardware acceptance

Cold boot with the HDMI cable and USB keyboard attached. Retain UART4 as an
independent recovery/login channel.

1. UART must report `ram size=0x40000000`.
2. The VM summary must account for both RAM banks without a region covering
   the `0x10000000..0x2fffffff` hole.
3. UART must report `dw-hdmi0: version=...` followed by
   `ci20 video: DRM HDMI/DVI 640x480 XRGB8888 ...`; the monitor must show
   the subsequent boot output and login prompt.
4. Verify separate login prompts on HDMI `/dev/console` and UART `/dev/ttyS0`.
   Input and command output from one session must not appear in the other.
5. Run `fbset`, `fbset fill 0x00ff0000`, and `fbview image.jpg` on HDMI.
6. Run a memory-pressure test large enough to allocate pages from the high
   bank, then repeat USB input and framebuffer output checks.

Hardware success must not be recorded from compilation alone. Preserve the
complete UART transcript and the exact image hashes when this gate is run.

## Hardware references

No Linux driver code is imported. The implementation is a clean ReBSD driver;
the sources below are used only for documented register addresses, bit fields,
clock parents, board wiring, and initialization ordering.

- The local JZ4780 programming manual, `docs/JZ4780_pm.pdf`, documents LCDC0,
  CPM pixel/HDMI clocks, GPIO, and the LCD DMA descriptors.
- MIPS CI20 Linux commit
  `7dff33297116643485ca37141d804eddd793e834`:
  [Ci20 memory and HDMI power wiring](https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/boot/dts/ci20.dts),
  [JZ4780 LCDC/HDMI addresses](https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/boot/dts/jz4780.dtsi),
  [LCDC sequencing](https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/drivers/gpu/drm/jz4780/jz4780_crtc.c), and
  [DWC HDMI sequencing](https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/drivers/gpu/drm/jz4780/dwc_hdmi.c).
- Current Linux DRM:
  [Ingenic LCDC/plane implementation](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/ingenic/ingenic-drm-drv.c)
  and
  [common Synopsys DW-HDMI bridge](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/bridge/synopsys/dw-hdmi.c).
- NetBSD DRM:
  [common DW-HDMI bridge](https://github.com/NetBSD/src/blob/trunk/sys/dev/ic/dw_hdmi.c)
  and
  [internal PHY implementation](https://github.com/NetBSD/src/blob/trunk/sys/dev/ic/dw_hdmi_phy.c).
- MIPS CI20 U-Boot commit
  `ef995a1611f0446a0b670ded9ec2609cb6dc51b7`:
  [PLL setup](https://github.com/MIPS/CI20_u-boot/blob/ef995a1611f0446a0b670ded9ec2609cb6dc51b7/arch/mips/cpu/xburst/jz4780/pll.c)
  establishes MPLL at 1.2 GHz and VPLL at 888 MHz.
