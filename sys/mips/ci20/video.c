/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>
#include <drm/drm_edid.h>
#include <drm/dw_hdmi.h>
#include <machine/layout.h>
#include <machine/video.h>
#include <vm/pmap.h>

#define CI20_CPM_BASE           0xb0000000u
#define CI20_GPIO_BASE          0xb0010000u
#define CI20_HDMI_BASE          0xb0180000u
#define CI20_LCDC0_BASE         0xb3050000u

#define CPM_CLKGR0              0x20
#define CPM_CLKGR1              0x28
#define CPM_CPMPCR              0x14
#define CPM_CPVPCR              0x1c
#define CPM_LP0CDR              0x54
#define CPM_HDMICDR             0x8c
#define CPM_SPCR0               0xb8
#define CPM_CLKGR0_LCD          (1u << 28)
#define CPM_CLKGR0_TVE          (1u << 27)
#define CPM_CLKGR1_HDMI         (1u << 9)
#define CPM_LPCDR_MPLL          (1u << 30)
#define CPM_LPCDR_CE            (1u << 28)
#define CPM_LPCDR_BUSY          (1u << 27)
#define CPM_LPCDR_STOP          (1u << 26)
#define CPM_HDMICDR_MPLL        (1u << 30)
#define CPM_HDMICDR_CE          (1u << 29)
#define CPM_HDMICDR_BUSY        (1u << 28)
#define CPM_HDMICDR_STOP        (1u << 26)
#define CPM_SPCR0_HDMI          (1u << 11)
#define CPM_SPCR0_LCD           (1u << 10)
#define CPM_PLL_M_SHIFT         19
#define CPM_PLL_M_MASK          0x1fffu
#define CPM_PLL_N_SHIFT         13
#define CPM_PLL_N_MASK          0x3fu
#define CPM_PLL_OD_SHIFT        9
#define CPM_PLL_OD_MASK         0xfu
#define CPM_PLL_ENABLE          (1u << 0)

#define GPIO_PA                 0
#define GPIO_PF                 5
#define GPIO_HDMI_POWER_PIN     25
#define GPIO_PXINTC(n)          (0x18u + (n) * 0x100u)
#define GPIO_PXMASKS(n)         (0x24u + (n) * 0x100u)
#define GPIO_PXMASKC(n)         (0x28u + (n) * 0x100u)
#define GPIO_PXPAT1C(n)         (0x38u + (n) * 0x100u)
#define GPIO_PXPAT0S(n)         (0x44u + (n) * 0x100u)
#define GPIO_PXPAT0C(n)         (0x48u + (n) * 0x100u)
#define GPIO_PXPENS(n)          (0x74u + (n) * 0x100u)
#define GPIO_HDMI_DDC_MASK      0x03000000u

#define LCDC_CFG                0x000
#define LCDC_VSYNC              0x004
#define LCDC_HSYNC              0x008
#define LCDC_VAT                0x00c
#define LCDC_DAH                0x010
#define LCDC_DAV                0x014
#define LCDC_CTRL               0x030
#define LCDC_STATE              0x034
#define LCDC_DA0                0x040
#define LCDC_SA0                0x044
#define LCDC_FID0               0x048
#define LCDC_CMD0               0x04c
#define LCDC_DA1                0x050
#define LCDC_SA1                0x054
#define LCDC_FID1               0x058
#define LCDC_CMD1               0x05c
#define LCDC_RGBC               0x090
#define LCDC_OSDC               0x100
#define LCDC_OSDCTRL            0x104
#define LCDC_OSDS               0x108
#define LCDC_XYP1               0x124
#define LCDC_SIZE1              0x12c
#define LCDC_PCFG               0x2c0

#define LCDC_CFG_NEWDES         (1u << 28)
#define LCDC_CFG_RECOVER        (1u << 25)
#define LCDC_CFG_PSM            (1u << 23)
#define LCDC_CFG_CLSM           (1u << 22)
#define LCDC_CFG_SPLM           (1u << 21)
#define LCDC_CFG_REVM           (1u << 20)
#define LCDC_CFG_HSP            (1u << 11)
#define LCDC_CFG_VSP            (1u << 8)
#define LCDC_CFG_TFT24          (1u << 6)
#define LCDC_CTRL_BST64         (4u << 28)
#define LCDC_CTRL_OFUP          (1u << 26)
#define LCDC_CTRL_OFUM          (1u << 11)
#define LCDC_CTRL_DIS           (1u << 4)
#define LCDC_CTRL_ENA           (1u << 3)
#define LCDC_STATE_IFU0         (1u << 2)
#define LCDC_STATE_IFU1         (1u << 1)
#define LCDC_STATE_LDD          (1u << 0)
#define LCDC_CMD_FRM_EN         (1u << 26)
#define LCDC_CPOS_BPP_24        (5u << 27)
#define LCDC_CPOS_COEF_1        (1u << 24)
#define LCDC_CPOS_XRGB8888      (LCDC_CPOS_BPP_24 | LCDC_CPOS_COEF_1)
#define LCDC_OSDC_OSDEN         (1u << 0)
#define LCDC_OSDC_ALPHAEN       (1u << 2)
#define LCDC_OSDC_F0EN          (1u << 3)
#define LCDC_OSDC_F1EN          (1u << 4)
#define LCDC_OSDCTRL_BPP_24     5u

#define HDMI_PHY_CONF0          0x3000
#define HDMI_PHY_STAT0          0x3004
#define HDMI_MC_CLKDIS          0x4001
#define HDMI_FC_INVIDCONF       0x1000
#define HDMI_FC_INHACTV0        0x1001
#define HDMI_FC_INHACTV1        0x1002
#define HDMI_FC_INVACTV0        0x1005
#define HDMI_FC_INVACTV1        0x1006

#define CI20_VIDEO_WIDTH        640u
#define CI20_VIDEO_HEIGHT       480u
#define CI20_VIDEO_BPP_BYTES    4u
#define CI20_VIDEO_STRIDE       (CI20_VIDEO_WIDTH * CI20_VIDEO_BPP_BYTES)
#define CI20_VIDEO_BYTES        (CI20_VIDEO_STRIDE * CI20_VIDEO_HEIGHT)
#define CI20_VIDEO_PIXEL_KHZ    25175u
#define CI20_VIDEO_MAP_HINT     0x08000000u
#define CI20_CLOCK_WAIT_US      10000u
#define CI20_EXCLK_KHZ          48000u
#define CI20_PIXEL_MIN_KHZ      13500u
#define CI20_PIXEL_MAX_KHZ      216000u
#define CI20_VIDEO_MAX_MODES    (DRM_EDID_MAX_MODES + 1u)

struct ci20_lcdc_descriptor {
    unsigned next;
    unsigned databuf;
    unsigned id;
    unsigned cmd;
    unsigned offsize;
    unsigned page_width;
    unsigned cpos;
    unsigned desc_size;
} __attribute__((packed));

struct ci20_pixel_clock {
    unsigned source;
    unsigned parent_khz;
    unsigned divider;
    unsigned actual_khz;
};

static struct ci20_lcdc_descriptor ci20_video_descriptors[2]
    __attribute__((section(".dma.video"), aligned(64)));
static struct drm_device ci20_drm_device;
static struct drm_mode_config ci20_drm_modes[CI20_VIDEO_MAX_MODES];
static struct dw_hdmi ci20_dw_hdmi;
static unsigned char ci20_edid[DRM_EDID_MAX_BYTES];
static struct drm_display_mode ci20_edid_modes[DRM_EDID_MAX_MODES];
static unsigned ci20_drm_mode_count;
static int ci20_video_initialized;
static int ci20_hdmi_powered;

static const struct drm_display_mode ci20_video_mode = {
    CI20_VIDEO_PIXEL_KHZ,
    640, 656, 752, 800,
    480, 490, 492, 525,
    DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct dw_hdmi_mpll_config ci20_hdmi_mpll[] = {
    { 45250, 0x01e0, 0x0000, 0x091c },
    { 58400, 0x0140, 0x0005, 0x091c },
    { 74250, 0x0140, 0x0005, 0x06dc },
    { 92500, 0x0140, 0x0005, 0x091c },
    { 118800, 0x00a0, 0x000a, 0x091c },
    { 216000, 0x00a0, 0x000a, 0x06dc },
    { 0, 0, 0, 0 },
};

static const struct dw_hdmi_phy_config ci20_hdmi_phy[] = {
    { 216000, 0x0005, 0x800d, 0x01ad },
    { 0, 0, 0, 0 },
};

static const struct dw_hdmi_plat_data ci20_hdmi_plat = {
    ci20_hdmi_mpll,
    ci20_hdmi_phy,
    0x69,
};

static int ci20_drm_enable(struct drm_device *,
    const struct drm_display_mode *, struct drm_framebuffer *);
static void ci20_drm_disable(struct drm_device *);
static void ci20_drm_mode_changed(struct drm_device *);

static const struct drm_driver ci20_drm_driver = {
    ci20_drm_enable,
    ci20_drm_disable,
    ci20_drm_mode_changed,
};

static volatile unsigned *
ci20_reg32(unsigned base, unsigned offset)
{
    return (volatile unsigned *)(base + offset);
}

static unsigned
ci20_read32(unsigned base, unsigned offset)
{
    return *ci20_reg32(base, offset);
}

static void
ci20_write32(unsigned base, unsigned offset, unsigned value)
{
    *ci20_reg32(base, offset) = value;
}

static volatile unsigned *
ci20_hdmi_reg(unsigned reg)
{
    return (volatile unsigned *)(CI20_HDMI_BASE + reg * 4u);
}

static unsigned
ci20_hdmi_read(void *cookie, unsigned reg)
{
    (void)cookie;
    return *ci20_hdmi_reg(reg) & 0xffu;
}

static void
ci20_hdmi_write(void *cookie, unsigned reg, unsigned value)
{
    (void)cookie;
    /* The JZ4780 DW-HDMI integration accepts 32-bit MMIO accesses. */
    *ci20_hdmi_reg(reg) = value & 0xffu;
}

static void
ci20_hdmi_delay(void *cookie, unsigned usec)
{
    (void)cookie;
    udelay(usec);
}

static int
ci20_clock_wait(unsigned reg, unsigned busy)
{
    unsigned elapsed;

    for (elapsed = 0; elapsed < CI20_CLOCK_WAIT_US; ++elapsed) {
        if ((ci20_read32(CI20_CPM_BASE, reg) & busy) == 0)
            return 0;
        udelay(1);
    }
    printf("ci20 video: clock timeout reg=%x value=%x busy=%x\n",
        reg, ci20_read32(CI20_CPM_BASE, reg), busy);
    return ETIMEDOUT;
}

static unsigned
ci20_pll_rate_khz(unsigned reg)
{
    unsigned divider;
    unsigned multiplier;
    unsigned value;

    value = ci20_read32(CI20_CPM_BASE, reg);
    if ((value & CPM_PLL_ENABLE) == 0)
        return 0;
    multiplier = ((value >> CPM_PLL_M_SHIFT) & CPM_PLL_M_MASK) + 1u;
    divider = (((value >> CPM_PLL_N_SHIFT) & CPM_PLL_N_MASK) + 1u) *
        (((value >> CPM_PLL_OD_SHIFT) & CPM_PLL_OD_MASK) + 1u);
    if (divider == 0 || multiplier > (unsigned)-1 / CI20_EXCLK_KHZ)
        return 0;
    return CI20_EXCLK_KHZ * multiplier / divider;
}

static int
ci20_pixel_clock_find(unsigned requested, struct ci20_pixel_clock *result)
{
    static const unsigned registers[] = { CPM_CPMPCR, CPM_CPVPCR };
    static const unsigned sources[] = { 1u << 30, 2u << 30 };
    struct ci20_pixel_clock candidate;
    unsigned best_delta;
    unsigned delta;
    unsigned i;

    if (requested == 0 || result == 0)
        return EINVAL;
    bzero(result, sizeof(*result));
    best_delta = (unsigned)-1;
    for (i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        candidate.parent_khz = ci20_pll_rate_khz(registers[i]);
        if (candidate.parent_khz == 0)
            continue;
        candidate.divider =
            (candidate.parent_khz + requested / 2u) / requested;
        if (candidate.divider == 0 || candidate.divider > 256u)
            continue;
        candidate.actual_khz =
            candidate.parent_khz / candidate.divider;
        candidate.source = sources[i];
        delta = candidate.actual_khz > requested ?
            candidate.actual_khz - requested :
            requested - candidate.actual_khz;
        if (delta < best_delta) {
            *result = candidate;
            best_delta = delta;
        }
    }
    return result->divider != 0 ? 0 : EINVAL;
}

static int
ci20_video_enable_clocks(const struct drm_display_mode *mode)
{
    struct ci20_pixel_clock pixel;
    unsigned value;
    int error;

    if (mode == 0 || mode->clock_khz < CI20_PIXEL_MIN_KHZ ||
        mode->clock_khz > CI20_PIXEL_MAX_KHZ)
        return EINVAL;
    error = ci20_pixel_clock_find(mode->clock_khz, &pixel);
    if (error != 0)
        return error;

    value = ci20_read32(CI20_CPM_BASE, CPM_SPCR0);
    value &= ~(CPM_SPCR0_HDMI | CPM_SPCR0_LCD);
    ci20_write32(CI20_CPM_BASE, CPM_SPCR0, value);

    /*
     * Keep the consumers gated while changing their non-glitch-free
     * source muxes.  The divider control registers remain accessible.
     */
    value = ci20_read32(CI20_CPM_BASE, CPM_CLKGR0);
    value |= CPM_CLKGR0_LCD | CPM_CLKGR0_TVE;
    ci20_write32(CI20_CPM_BASE, CPM_CLKGR0, value);

    value = ci20_read32(CI20_CPM_BASE, CPM_CLKGR1);
    value |= CPM_CLKGR1_HDMI;
    ci20_write32(CI20_CPM_BASE, CPM_CLKGR1, value);

    /* The LCD pixel mux is not glitch-free: stop it before changing LPCS. */
    value = ci20_read32(CI20_CPM_BASE, CPM_LP0CDR);
    value &= ~(CPM_LPCDR_BUSY | CPM_LPCDR_CE);
    value |= CPM_LPCDR_CE | CPM_LPCDR_STOP;
    ci20_write32(CI20_CPM_BASE, CPM_LP0CDR, value);
    error = ci20_clock_wait(CPM_LP0CDR, CPM_LPCDR_BUSY);
    if (error != 0)
        return error;

    /* Select the closest fixed video clock from MPLL or VPLL. */
    value = ci20_read32(CI20_CPM_BASE, CPM_LP0CDR);
    value &= ~(0xc0000000u | CPM_LPCDR_CE | CPM_LPCDR_BUSY |
        CPM_LPCDR_STOP | 0xffu);
    value |= pixel.source | CPM_LPCDR_CE | CPM_LPCDR_STOP |
        (pixel.divider - 1u);
    ci20_write32(CI20_CPM_BASE, CPM_LP0CDR, value);
    error = ci20_clock_wait(CPM_LP0CDR, CPM_LPCDR_BUSY);
    if (error != 0)
        return error;

    value = ci20_read32(CI20_CPM_BASE, CPM_LP0CDR);
    value &= ~CPM_LPCDR_STOP;
    value |= CPM_LPCDR_CE;
    ci20_write32(CI20_CPM_BASE, CPM_LP0CDR, value);
    error = ci20_clock_wait(CPM_LP0CDR, CPM_LPCDR_BUSY);
    if (error != 0)
        return error;

    value = ci20_read32(CI20_CPM_BASE, CPM_CLKGR0);
    value &= ~(CPM_CLKGR0_LCD | CPM_CLKGR0_TVE);
    ci20_write32(CI20_CPM_BASE, CPM_CLKGR0, value);

    /* The HDMI mux is not glitch-free: stop it before changing HPCS. */
    value = ci20_read32(CI20_CPM_BASE, CPM_HDMICDR);
    value &= ~(CPM_HDMICDR_BUSY | CPM_HDMICDR_CE);
    value |= CPM_HDMICDR_CE | CPM_HDMICDR_STOP;
    ci20_write32(CI20_CPM_BASE, CPM_HDMICDR, value);
    error = ci20_clock_wait(CPM_HDMICDR, CPM_HDMICDR_BUSY);
    if (error != 0)
        return error;

    /* MPLL is already verified by the running USB host clock. */
    value = ci20_read32(CI20_CPM_BASE, CPM_HDMICDR);
    value &= ~(0xc0000000u | CPM_HDMICDR_CE | CPM_HDMICDR_BUSY |
        CPM_HDMICDR_STOP | 0xffu);
    /* 1.2 GHz / 45 gives 26.67 MHz, inside the required 18-27 MHz. */
    value |= CPM_HDMICDR_MPLL | CPM_HDMICDR_CE |
        CPM_HDMICDR_STOP | 44u;
    ci20_write32(CI20_CPM_BASE, CPM_HDMICDR, value);
    error = ci20_clock_wait(CPM_HDMICDR, CPM_HDMICDR_BUSY);
    if (error != 0)
        return error;

    value &= ~CPM_HDMICDR_STOP;
    value |= CPM_HDMICDR_CE;
    ci20_write32(CI20_CPM_BASE, CPM_HDMICDR, value);
    error = ci20_clock_wait(CPM_HDMICDR, CPM_HDMICDR_BUSY);
    if (error != 0)
        return error;

    value = ci20_read32(CI20_CPM_BASE, CPM_CLKGR1);
    value &= ~CPM_CLKGR1_HDMI;
    ci20_write32(CI20_CPM_BASE, CPM_CLKGR1, value);
    udelay(10);
    printf("ci20 video: pixel clock requested=%uKHz actual=%uKHz "
        "parent=%s/%uKHz div=%u\n",
        mode->clock_khz, pixel.actual_khz,
        pixel.source == CPM_LPCDR_MPLL ? "mpll" : "vpll",
        pixel.parent_khz, pixel.divider);
    return 0;
}

static void
ci20_hdmi_ddc_pinmux(void)
{
    /*
     * JZ4780 PF24/PF25 function 0 is the dedicated DW-HDMI DDC bus.
     * Match Linux pinctrl's "hdmi-ddc" group and disable internal pulls.
     */
    ci20_write32(CI20_GPIO_BASE, GPIO_PXINTC(GPIO_PF),
        GPIO_HDMI_DDC_MASK);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXMASKC(GPIO_PF),
        GPIO_HDMI_DDC_MASK);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT1C(GPIO_PF),
        GPIO_HDMI_DDC_MASK);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT0C(GPIO_PF),
        GPIO_HDMI_DDC_MASK);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPENS(GPIO_PF),
        GPIO_HDMI_DDC_MASK);
}

static void
ci20_hdmi_power_on(void)
{
    unsigned bit;

    ci20_hdmi_ddc_pinmux();
    if (ci20_hdmi_powered)
        return;
    bit = 1u << GPIO_HDMI_POWER_PIN;
    ci20_write32(CI20_GPIO_BASE, GPIO_PXINTC(GPIO_PA), bit);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXMASKS(GPIO_PA), bit);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT1C(GPIO_PA), bit);
    /* PA25 high drives the Ci20 HDMI 5 V power-switch transistor. */
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT0S(GPIO_PA), bit);
    /* Linux enables this always-on regulator well before HDMI probes. */
    udelay(100000);
    ci20_hdmi_powered = 1;
}

static void
ci20_lcdc_stop(void)
{
    unsigned ctrl;
    unsigned elapsed;

    ctrl = ci20_read32(CI20_LCDC0_BASE, LCDC_CTRL);
    if ((ctrl & LCDC_CTRL_ENA) == 0)
        return;
    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL, ctrl | LCDC_CTRL_DIS);
    for (elapsed = 0; elapsed < CI20_CLOCK_WAIT_US; ++elapsed) {
        if ((ci20_read32(CI20_LCDC0_BASE, LCDC_STATE) &
            LCDC_STATE_LDD) != 0)
            break;
        udelay(1);
    }
    ci20_write32(CI20_LCDC0_BASE, LCDC_STATE, 0);
    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL,
        ctrl & ~(LCDC_CTRL_DIS | LCDC_CTRL_ENA));
}

static void
ci20_lcdc_start(const struct drm_display_mode *mode,
    const struct drm_framebuffer *fb)
{
    volatile struct ci20_lcdc_descriptor *descriptor;
    unsigned descriptor_phys;
    unsigned descriptor1_phys;
    unsigned frame_words;
    unsigned size;
    unsigned hsync;
    unsigned hback;
    unsigned hactive_start;
    unsigned vsync;
    unsigned vback;
    unsigned vactive_start;
    unsigned cfg;
    unsigned ctrl;

    descriptor_phys = MIPS_KSEG_TO_PHYS(ci20_video_descriptors);
    descriptor1_phys = descriptor_phys + sizeof(*descriptor);
    descriptor = (volatile struct ci20_lcdc_descriptor *)
        MIPS_PHYS_TO_KSEG1(descriptor_phys);
    frame_words = fb->bytes / sizeof(unsigned);
    size = (0xffu << 24) | ((mode->vdisplay - 1) << 12) |
        (mode->hdisplay - 1);
    hsync = mode->hsync_end - mode->hsync_start;
    hback = mode->htotal - mode->hsync_end;
    hactive_start = hsync + hback;
    vsync = mode->vsync_end - mode->vsync_start;
    vback = mode->vtotal - mode->vsync_end;
    vactive_start = vsync + vback;

    /*
     * JZ4780 foreground 0 is unusable for HDMI scanout.  Keep an inert,
     * self-linked F0 descriptor and put the framebuffer on foreground 1.
     */
    descriptor[0].next = descriptor_phys;
    descriptor[0].databuf = 0;
    descriptor[0].id = 0xf0;
    descriptor[0].cmd = 0;
    descriptor[0].offsize = 0;
    descriptor[0].page_width = 0;
    descriptor[0].cpos = 0;
    descriptor[0].desc_size = 0;

    descriptor[1].next = descriptor1_phys;
    descriptor[1].databuf = fb->paddr;
    descriptor[1].id = 0xf1;
    descriptor[1].cmd = LCDC_CMD_FRM_EN | frame_words;
    descriptor[1].offsize = 0;
    descriptor[1].page_width = 0;
    descriptor[1].cpos = LCDC_CPOS_XRGB8888;
    descriptor[1].desc_size = size;
    asm volatile ("sync" ::: "memory");

    cfg = LCDC_CFG_NEWDES | LCDC_CFG_RECOVER | LCDC_CFG_TFT24 |
        LCDC_CFG_PSM | LCDC_CFG_CLSM | LCDC_CFG_SPLM | LCDC_CFG_REVM;
    if ((mode->flags & DRM_MODE_FLAG_NHSYNC) != 0)
        cfg |= LCDC_CFG_HSP;
    if ((mode->flags & DRM_MODE_FLAG_NVSYNC) != 0)
        cfg |= LCDC_CFG_VSP;
    ctrl = LCDC_CTRL_BST64 | LCDC_CTRL_OFUP | LCDC_CTRL_OFUM;

    ci20_lcdc_stop();
    ci20_write32(CI20_LCDC0_BASE, LCDC_STATE, 0);
    ci20_write32(CI20_LCDC0_BASE, LCDC_OSDS, 0);
    ci20_write32(CI20_LCDC0_BASE, LCDC_VAT,
        (mode->htotal << 16) | mode->vtotal);
    ci20_write32(CI20_LCDC0_BASE, LCDC_DAH,
        (hactive_start << 16) | (hactive_start + mode->hdisplay));
    ci20_write32(CI20_LCDC0_BASE, LCDC_DAV,
        (vactive_start << 16) | (vactive_start + mode->vdisplay));
    ci20_write32(CI20_LCDC0_BASE, LCDC_HSYNC, hsync);
    ci20_write32(CI20_LCDC0_BASE, LCDC_VSYNC, vsync);
    ci20_write32(CI20_LCDC0_BASE, LCDC_CFG, cfg);
    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL, ctrl);
    ci20_write32(CI20_LCDC0_BASE, LCDC_PCFG,
        0xc0000000u | (511u << 18) | (400u << 9) | 256u);
    ci20_write32(CI20_LCDC0_BASE, LCDC_RGBC, 0);
    ci20_write32(CI20_LCDC0_BASE, LCDC_OSDCTRL,
        LCDC_OSDCTRL_BPP_24);
    ci20_write32(CI20_LCDC0_BASE, LCDC_XYP1, 0);
    ci20_write32(CI20_LCDC0_BASE, LCDC_SIZE1,
        (mode->vdisplay << 16) | mode->hdisplay);
    ci20_write32(CI20_LCDC0_BASE, LCDC_OSDC,
        LCDC_OSDC_OSDEN | LCDC_OSDC_ALPHAEN | LCDC_OSDC_F1EN);
    ci20_write32(CI20_LCDC0_BASE, LCDC_DA0, descriptor_phys);
    ci20_write32(CI20_LCDC0_BASE, LCDC_DA1, descriptor1_phys);
    asm volatile ("sync" ::: "memory");
    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL,
        ctrl | LCDC_CTRL_ENA);
}

static void
ci20_video_dump_state(void)
{
    unsigned state;

    state = ci20_read32(CI20_LCDC0_BASE, LCDC_STATE);
    printf("ci20 video: HDMI conf=%x stat=%x mcclk=%x fc=%x "
        "hact=%x:%x vact=%x:%x\n",
        ci20_hdmi_read(0, HDMI_PHY_CONF0),
        ci20_hdmi_read(0, HDMI_PHY_STAT0),
        ci20_hdmi_read(0, HDMI_MC_CLKDIS),
        ci20_hdmi_read(0, HDMI_FC_INVIDCONF),
        ci20_hdmi_read(0, HDMI_FC_INHACTV1),
        ci20_hdmi_read(0, HDMI_FC_INHACTV0),
        ci20_hdmi_read(0, HDMI_FC_INVACTV1),
        ci20_hdmi_read(0, HDMI_FC_INVACTV0));
    printf("ci20 video: LCD clkgr0=%x lp0cdr=%x cfg=%x ctrl=%x "
        "state=%x osdc=%x osdctrl=%x\n",
        ci20_read32(CI20_CPM_BASE, CPM_CLKGR0),
        ci20_read32(CI20_CPM_BASE, CPM_LP0CDR),
        ci20_read32(CI20_LCDC0_BASE, LCDC_CFG),
        ci20_read32(CI20_LCDC0_BASE, LCDC_CTRL),
        state,
        ci20_read32(CI20_LCDC0_BASE, LCDC_OSDC),
        ci20_read32(CI20_LCDC0_BASE, LCDC_OSDCTRL));
    printf("ci20 video: DMA da0=%x sa0=%x fid0=%x cmd0=%x "
        "da1=%x sa1=%x fid1=%x cmd1=%x\n",
        ci20_read32(CI20_LCDC0_BASE, LCDC_DA0),
        ci20_read32(CI20_LCDC0_BASE, LCDC_SA0),
        ci20_read32(CI20_LCDC0_BASE, LCDC_FID0),
        ci20_read32(CI20_LCDC0_BASE, LCDC_CMD0),
        ci20_read32(CI20_LCDC0_BASE, LCDC_DA1),
        ci20_read32(CI20_LCDC0_BASE, LCDC_SA1),
        ci20_read32(CI20_LCDC0_BASE, LCDC_FID1),
        ci20_read32(CI20_LCDC0_BASE, LCDC_CMD1));
    printf("ci20 video: LCD vat=%x dah=%x dav=%x hsync=%x vsync=%x\n",
        ci20_read32(CI20_LCDC0_BASE, LCDC_VAT),
        ci20_read32(CI20_LCDC0_BASE, LCDC_DAH),
        ci20_read32(CI20_LCDC0_BASE, LCDC_DAV),
        ci20_read32(CI20_LCDC0_BASE, LCDC_HSYNC),
        ci20_read32(CI20_LCDC0_BASE, LCDC_VSYNC));
    if ((state & (LCDC_STATE_IFU0 | LCDC_STATE_IFU1)) != 0)
        printf("ci20 video: LCD input FIFO underrun%s%s\n",
            (state & LCDC_STATE_IFU0) != 0 ? " IFU0" : "",
            (state & LCDC_STATE_IFU1) != 0 ? " IFU1" : "");
}

static int
ci20_mode_equal(const struct drm_display_mode *a,
    const struct drm_display_mode *b)
{
    return a->clock_khz == b->clock_khz &&
        a->hdisplay == b->hdisplay &&
        a->hsync_start == b->hsync_start &&
        a->hsync_end == b->hsync_end &&
        a->htotal == b->htotal &&
        a->vdisplay == b->vdisplay &&
        a->vsync_start == b->vsync_start &&
        a->vsync_end == b->vsync_end &&
        a->vtotal == b->vtotal &&
        a->flags == b->flags;
}

static int
ci20_mode_supported(const struct drm_display_mode *mode)
{
    struct ci20_pixel_clock pixel;
    unsigned bytes;
    unsigned delta;
    unsigned stride;

    if (mode == 0 || mode->clock_khz < CI20_PIXEL_MIN_KHZ ||
        mode->clock_khz > CI20_PIXEL_MAX_KHZ ||
        mode->hdisplay >= mode->hsync_start ||
        mode->hsync_start >= mode->hsync_end ||
        mode->hsync_end >= mode->htotal ||
        mode->vdisplay >= mode->vsync_start ||
        mode->vsync_start >= mode->vsync_end ||
        mode->vsync_end >= mode->vtotal ||
        mode->hdisplay < CI20_VIDEO_WIDTH ||
        mode->vdisplay < CI20_VIDEO_HEIGHT ||
        mode->hdisplay > 2048u || mode->vdisplay > 2048u ||
        (mode->flags & DRM_MODE_FLAG_INTERLACE) != 0 ||
        mode->htotal > 0xffffu || mode->vtotal > 0xffffu ||
        mode->vtotal - mode->vdisplay > 0xffu ||
        mode->hsync_start - mode->hdisplay > 0xffffu ||
        mode->hsync_end - mode->hsync_start > 0xffffu ||
        mode->vsync_start - mode->vdisplay > 0xffu ||
        mode->vsync_end - mode->vsync_start > 0xffu)
        return 0;
    if (mode->hdisplay > (unsigned)-1 / CI20_VIDEO_BPP_BYTES)
        return 0;
    stride = mode->hdisplay * CI20_VIDEO_BPP_BYTES;
    if (mode->vdisplay > (unsigned)-1 / stride)
        return 0;
    bytes = stride * mode->vdisplay;
    if (bytes > CI20_FRAMEBUFFER_BYTES)
        return 0;
    if (ci20_pixel_clock_find(mode->clock_khz, &pixel) != 0)
        return 0;
    delta = pixel.actual_khz > mode->clock_khz ?
        pixel.actual_khz - mode->clock_khz :
        mode->clock_khz - pixel.actual_khz;
    return delta * 100u <= mode->clock_khz * 5u;
}

static void
ci20_mode_framebuffer(struct drm_mode_config *config,
    const struct drm_display_mode *mode)
{
    struct drm_framebuffer *framebuffer;

    bzero(config, sizeof(*config));
    config->mode = *mode;
    framebuffer = &config->framebuffer;
    framebuffer->vaddr = (volatile unsigned char *)
        MIPS_PHYS_TO_KSEG1(CI20_FRAMEBUFFER_PHYS_START);
    framebuffer->paddr = CI20_FRAMEBUFFER_PHYS_START;
    framebuffer->width = mode->hdisplay;
    framebuffer->height = mode->vdisplay;
    framebuffer->stride = mode->hdisplay * CI20_VIDEO_BPP_BYTES;
    framebuffer->bytes = framebuffer->stride * framebuffer->height;
    framebuffer->reserved_bytes = CI20_FRAMEBUFFER_BYTES;
    framebuffer->map_hint = CI20_VIDEO_MAP_HINT;
    framebuffer->bpp = 32;
    framebuffer->format = DRM_FORMAT_XRGB8888;
    framebuffer->cache_mode = PMAP_CACHE_UNCACHED;
}

static int
ci20_mode_add(const struct drm_display_mode *mode)
{
    unsigned i;

    if (!ci20_mode_supported(mode))
        return EINVAL;
    for (i = 0; i < ci20_drm_mode_count; ++i)
        if (ci20_mode_equal(&ci20_drm_modes[i].mode, mode))
            return 0;
    if (ci20_drm_mode_count >= CI20_VIDEO_MAX_MODES)
        return ENOSPC;
    ci20_mode_framebuffer(&ci20_drm_modes[ci20_drm_mode_count], mode);
    ++ci20_drm_mode_count;
    return 0;
}

static unsigned
ci20_mode_score(const struct drm_display_mode *mode)
{
    return mode->hdisplay * mode->vdisplay;
}

static void
ci20_build_mode_list(const struct drm_edid_info *info,
    unsigned edid_mode_count)
{
    unsigned best;
    unsigned best_score;
    unsigned preferred;
    unsigned score;
    unsigned i;

    ci20_drm_mode_count = 0;
    best = DRM_EDID_NO_PREFERRED;
    best_score = 0;
    preferred = info != 0 ? info->preferred_mode :
        DRM_EDID_NO_PREFERRED;
    if (preferred < edid_mode_count &&
        ci20_mode_supported(&ci20_edid_modes[preferred]))
        best = preferred;
    if (best == DRM_EDID_NO_PREFERRED)
        for (i = 0; i < edid_mode_count; ++i) {
            if (!ci20_mode_supported(&ci20_edid_modes[i]))
                continue;
            score = ci20_mode_score(&ci20_edid_modes[i]);
            if (best == DRM_EDID_NO_PREFERRED || score > best_score ||
                (score == best_score &&
                ci20_edid_modes[i].clock_khz >
                ci20_edid_modes[best].clock_khz)) {
                best = i;
                best_score = score;
            }
        }
    if (best != DRM_EDID_NO_PREFERRED)
        (void)ci20_mode_add(&ci20_edid_modes[best]);
    for (i = 0; i < edid_mode_count; ++i)
        if (i != best)
            (void)ci20_mode_add(&ci20_edid_modes[i]);
    (void)ci20_mode_add(&ci20_video_mode);
}

static unsigned
ci20_read_edid(struct drm_edid_info *info)
{
    unsigned bytes;
    unsigned mode_count;
    int error;

    bytes = 0;
    mode_count = 0;
    if (!dw_hdmi_hpd(&ci20_dw_hdmi)) {
        printf("ci20 video: HDMI connector not detected; using fallback\n");
        return 0;
    }
    error = drm_edid_read(dw_hdmi_ddc_adapter(&ci20_dw_hdmi),
        ci20_edid, sizeof(ci20_edid), &bytes);
    if (error != 0) {
        printf("ci20 video: EDID read failed, error=%d; using fallback\n",
            error);
        return 0;
    }
    error = drm_edid_parse(ci20_edid, bytes, info, ci20_edid_modes,
        DRM_EDID_MAX_MODES, &mode_count);
    if (error != 0) {
        printf("ci20 video: EDID parse failed, error=%d; using fallback\n",
            error);
        return 0;
    }
    printf("ci20 video: EDID %s product=%x name=\"%s\" blocks=%u "
        "modes=%u\n", info->vendor, info->product,
        info->monitor_name[0] != '\0' ? info->monitor_name : "unknown",
        bytes / DRM_EDID_BLOCK_BYTES, mode_count);
    return mode_count;
}

static int
ci20_drm_enable(struct drm_device *dev,
    const struct drm_display_mode *mode, struct drm_framebuffer *fb)
{
    volatile unsigned *pixels;
    unsigned count;
    unsigned i;
    int error;

    ci20_hdmi_power_on();
    if (dev->crtc.enabled) {
        dw_hdmi_disable(&ci20_dw_hdmi);
        ci20_lcdc_stop();
    }
    error = ci20_video_enable_clocks(mode);
    if (error != 0)
        return error;
    printf("ci20 video: clocks clkgr1=%x hdmicdr=%x mpll=%x "
        "vpll=%x spcr0=%x\n",
        ci20_read32(CI20_CPM_BASE, CPM_CLKGR1),
        ci20_read32(CI20_CPM_BASE, CPM_HDMICDR),
        ci20_read32(CI20_CPM_BASE, CPM_CPMPCR),
        ci20_read32(CI20_CPM_BASE, CPM_CPVPCR),
        ci20_read32(CI20_CPM_BASE, CPM_SPCR0));

    pixels = (volatile unsigned *)fb->vaddr;
    count = fb->bytes / sizeof(*pixels);
    for (i = 0; i < count; ++i)
        pixels[i] = 0x00102030u;
    asm volatile ("sync" ::: "memory");

    /*
     * The JZ4780 LCDC supplies the live input pixel clock required while
     * the common DW-HDMI bridge initializes and locks its PHY.
     */
    ci20_lcdc_start(mode, fb);
    udelay(10000);
    error = dw_hdmi_enable(&ci20_dw_hdmi, mode);
    dev->phy_status = ci20_dw_hdmi.phy_status;
    ci20_video_dump_state();
    if (error != 0) {
        dw_hdmi_disable(&ci20_dw_hdmi);
        ci20_lcdc_stop();
        return error;
    }
    return 0;
}

static void
ci20_drm_disable(struct drm_device *dev)
{
    (void)dev;
    dw_hdmi_disable(&ci20_dw_hdmi);
    ci20_lcdc_stop();
}

static void
ci20_drm_mode_changed(struct drm_device *dev)
{
    (void)dev;
    ci20_video_console_mode_changed();
}

volatile unsigned *
ci20_video_framebuffer(void)
{
    return (volatile unsigned *)
        MIPS_PHYS_TO_KSEG1(CI20_FRAMEBUFFER_PHYS_START);
}

void
ci20_video_clear(unsigned color)
{
    volatile unsigned *fb;
    unsigned pixels;
    unsigned i;

    fb = ci20_video_framebuffer();
    if (ci20_video_initialized)
        pixels = ci20_drm_device.framebuffer.bytes / sizeof(*fb);
    else
        pixels = CI20_VIDEO_WIDTH * CI20_VIDEO_HEIGHT;
    for (i = 0; i < pixels; ++i)
        fb[i] = color;
    asm volatile ("sync" ::: "memory");
}

int
ci20_video_ready(void)
{
    return ci20_video_initialized;
}

void
ci20_video_get_info(struct drmfb_info *info)
{
    const struct drm_framebuffer *framebuffer;
    const struct drm_display_mode *mode;

    if (info == 0)
        return;
    bzero(info, sizeof(*info));
    if (ci20_video_initialized) {
        framebuffer = &ci20_drm_device.framebuffer;
        mode = &ci20_drm_device.crtc.mode;
    } else {
        framebuffer = &ci20_drm_modes[0].framebuffer;
        mode = &ci20_drm_modes[0].mode;
    }
    info->width = framebuffer->width;
    info->height = framebuffer->height;
    info->stride = framebuffer->stride;
    info->bpp = 32;
    info->format = DRM_FORMAT_XRGB8888;
    info->fb_phys = CI20_FRAMEBUFFER_PHYS_START;
    info->fb_bytes = framebuffer->bytes;
    info->reserved_bytes = CI20_FRAMEBUFFER_BYTES;
    info->pixel_clock_khz = mode->clock_khz;
    info->phy_status = ci20_drm_device.phy_status;
    info->mode_index = ci20_drm_device.mode_index;
    info->mode_count = ci20_drm_device.mode_count;
}

void
ci20_video_attach(void)
{
    struct drm_edid_info edid_info;
    const struct drm_mode_config *config;
    unsigned edid_mode_count;
    int error;

    if (ci20_video_initialized)
        return;

    error = dw_hdmi_init(&ci20_dw_hdmi, "dw-hdmi0", 0,
        ci20_hdmi_read, ci20_hdmi_write, ci20_hdmi_delay,
        &ci20_hdmi_plat);
    if (error != 0) {
        printf("ci20 video: DW-HDMI attach failed, error=%d\n", error);
        return;
    }

    ci20_hdmi_power_on();
    error = ci20_video_enable_clocks(&ci20_video_mode);
    if (error != 0) {
        printf("ci20 video: DDC clock setup failed, error=%d\n", error);
        return;
    }
    bzero(&edid_info, sizeof(edid_info));
    edid_mode_count = ci20_read_edid(&edid_info);
    ci20_build_mode_list(edid_mode_count != 0 ? &edid_info : 0,
        edid_mode_count);
    if (ci20_drm_mode_count == 0) {
        printf("ci20 video: no usable display mode\n");
        return;
    }

    ci20_drm_device.name = "jz4780-drm";
    ci20_drm_device.driver = &ci20_drm_driver;
    ci20_drm_device.driver_private = 0;
    ci20_drm_device.connector.name = "HDMI-A-1";
    ci20_drm_device.modes = ci20_drm_modes;
    ci20_drm_device.mode_count = ci20_drm_mode_count;

    error = drm_device_register(&ci20_drm_device);
    if (error != 0) {
        printf("ci20 video: DRM registration failed, error=%d\n", error);
        return;
    }
    error = drm_mode_set_index(&ci20_drm_device, 0);
    if (error != 0) {
        printf("ci20 video: HDMI initialization failed, error=%d\n",
            error);
        return;
    }
    ci20_video_initialized = 1;
    config = &ci20_drm_modes[ci20_drm_device.mode_index];
    printf("ci20 video: DRM HDMI/DVI %ux%u XRGB8888 "
        "clock=%uKHz mode=%u/%u fb=%x bytes=%u phy=%x\n",
        config->mode.hdisplay, config->mode.vdisplay,
        config->mode.clock_khz, ci20_drm_device.mode_index,
        ci20_drm_device.mode_count, CI20_FRAMEBUFFER_PHYS_START,
        config->framebuffer.bytes, ci20_drm_device.phy_status);
}
