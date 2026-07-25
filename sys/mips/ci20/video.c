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

#define GPIO_PA                 0
#define GPIO_HDMI_POWER_PIN     25
#define GPIO_PXINTC(n)          (0x18u + (n) * 0x100u)
#define GPIO_PXMASKS(n)         (0x24u + (n) * 0x100u)
#define GPIO_PXPAT1C(n)         (0x38u + (n) * 0x100u)
#define GPIO_PXPAT0S(n)         (0x44u + (n) * 0x100u)

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
#define LCDC_CTRL_OFUM          (1u << 11)
#define LCDC_CTRL_DIS           (1u << 4)
#define LCDC_CTRL_ENA           (1u << 3)
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
#define CI20_VIDEO_PIXEL_KHZ    25000u
#define CI20_VIDEO_MAP_HINT     0x08000000u
#define CI20_CLOCK_WAIT_US      10000u

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

static struct ci20_lcdc_descriptor ci20_video_descriptors[2]
    __attribute__((section(".dma.video"), aligned(64)));
static struct drm_device ci20_drm_device;
static struct drm_mode_config ci20_drm_mode;
static struct dw_hdmi ci20_dw_hdmi;
static int ci20_video_initialized;

static const struct drm_display_mode ci20_video_mode = {
    CI20_VIDEO_PIXEL_KHZ,
    640, 656, 752, 800,
    480, 490, 492, 525,
    DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct dw_hdmi_mpll_config ci20_hdmi_mpll[] = {
    { 65000, 0x01e0, 0x0000, 0x091c },
    { 0, 0, 0, 0 },
};

static const struct dw_hdmi_phy_config ci20_hdmi_phy[] = {
    { 65000, 0x0005, 0x800d, 0x01ad },
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

static const struct drm_driver ci20_drm_driver = {
    ci20_drm_enable,
    ci20_drm_disable,
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

static int
ci20_video_enable_clocks(void)
{
    unsigned value;
    int error;

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

    /* 1.2 GHz MPLL / 48 gives a monitor-safe 25 MHz VGA pixel clock. */
    value = ci20_read32(CI20_CPM_BASE, CPM_LP0CDR);
    value &= ~(0xc0000000u | CPM_LPCDR_CE | CPM_LPCDR_BUSY |
        CPM_LPCDR_STOP | 0xffu);
    value |= CPM_LPCDR_MPLL | CPM_LPCDR_CE | CPM_LPCDR_STOP | 47u;
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
    return 0;
}

static void
ci20_hdmi_power_on(void)
{
    unsigned bit;

    bit = 1u << GPIO_HDMI_POWER_PIN;
    ci20_write32(CI20_GPIO_BASE, GPIO_PXINTC(GPIO_PA), bit);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXMASKS(GPIO_PA), bit);
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT1C(GPIO_PA), bit);
    /* PA25 high drives the Ci20 HDMI 5 V power-switch transistor. */
    ci20_write32(CI20_GPIO_BASE, GPIO_PXPAT0S(GPIO_PA), bit);
    /* Linux enables this always-on regulator well before HDMI probes. */
    udelay(100000);
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
    ctrl = LCDC_CTRL_BST64 | LCDC_CTRL_OFUM;

    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL,
        ci20_read32(CI20_LCDC0_BASE, LCDC_CTRL) & ~LCDC_CTRL_ENA);
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
        ci20_read32(CI20_LCDC0_BASE, LCDC_STATE),
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
    error = ci20_video_enable_clocks();
    if (error != 0)
        return error;
    printf("ci20 video: clocks clkgr1=%x hdmicdr=%x mpll=%x "
        "vpll=%x spcr0=%x\n",
        ci20_read32(CI20_CPM_BASE, CPM_CLKGR1),
        ci20_read32(CI20_CPM_BASE, CPM_HDMICDR),
        ci20_read32(CI20_CPM_BASE, CPM_CPMPCR),
        ci20_read32(CI20_CPM_BASE, CPM_CPVPCR),
        ci20_read32(CI20_CPM_BASE, CPM_SPCR0));

    if (!ci20_video_initialized) {
        pixels = (volatile unsigned *)fb->vaddr;
        count = fb->bytes / sizeof(*pixels);
        for (i = 0; i < count; ++i)
            pixels[i] = 0x00102030u;
        asm volatile ("sync" ::: "memory");
    }

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
        ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL,
            ci20_read32(CI20_LCDC0_BASE, LCDC_CTRL) & ~LCDC_CTRL_ENA);
        return error;
    }
    return 0;
}

static void
ci20_drm_disable(struct drm_device *dev)
{
    (void)dev;
    dw_hdmi_disable(&ci20_dw_hdmi);
    ci20_write32(CI20_LCDC0_BASE, LCDC_CTRL,
        ci20_read32(CI20_LCDC0_BASE, LCDC_CTRL) & ~LCDC_CTRL_ENA);
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
    if (info == 0)
        return;
    bzero(info, sizeof(*info));
    info->width = CI20_VIDEO_WIDTH;
    info->height = CI20_VIDEO_HEIGHT;
    info->stride = CI20_VIDEO_STRIDE;
    info->bpp = 32;
    info->format = DRM_FORMAT_XRGB8888;
    info->fb_phys = CI20_FRAMEBUFFER_PHYS_START;
    info->fb_bytes = CI20_VIDEO_BYTES;
    info->reserved_bytes = CI20_FRAMEBUFFER_BYTES;
    info->pixel_clock_khz = CI20_VIDEO_PIXEL_KHZ;
    info->phy_status = ci20_drm_device.phy_status;
    info->mode_index = ci20_drm_device.mode_index;
    info->mode_count = ci20_drm_device.mode_count;
}

void
ci20_video_attach(void)
{
    struct drm_framebuffer *framebuffer;
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

    ci20_drm_device.name = "jz4780-drm";
    ci20_drm_device.driver = &ci20_drm_driver;
    ci20_drm_device.driver_private = 0;
    ci20_drm_device.connector.name = "HDMI-A-1";
    ci20_drm_device.modes = &ci20_drm_mode;
    ci20_drm_device.mode_count = 1;
    bzero(&ci20_drm_mode, sizeof(ci20_drm_mode));
    ci20_drm_mode.mode = ci20_video_mode;
    framebuffer = &ci20_drm_mode.framebuffer;
    framebuffer->vaddr = (volatile unsigned char *)
        MIPS_PHYS_TO_KSEG1(CI20_FRAMEBUFFER_PHYS_START);
    framebuffer->paddr = CI20_FRAMEBUFFER_PHYS_START;
    framebuffer->bytes = CI20_VIDEO_BYTES;
    framebuffer->reserved_bytes = CI20_FRAMEBUFFER_BYTES;
    framebuffer->map_hint = CI20_VIDEO_MAP_HINT;
    framebuffer->width = CI20_VIDEO_WIDTH;
    framebuffer->height = CI20_VIDEO_HEIGHT;
    framebuffer->stride = CI20_VIDEO_STRIDE;
    framebuffer->bpp = 32;
    framebuffer->format = DRM_FORMAT_XRGB8888;
    framebuffer->cache_mode = PMAP_CACHE_UNCACHED;

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
    printf("ci20 video: DRM HDMI/DVI %ux%u XRGB8888 "
        "fb=%x bytes=%u phy=%x\n",
        CI20_VIDEO_WIDTH, CI20_VIDEO_HEIGHT,
        CI20_FRAMEBUFFER_PHYS_START, CI20_VIDEO_BYTES,
        ci20_drm_device.phy_status);
}
