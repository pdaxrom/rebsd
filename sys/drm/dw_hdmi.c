/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Common Synopsys DesignWare HDMI bridge.  The register programming follows
 * the DRM bridge sequence used by Linux and NetBSD.  Platform drivers provide
 * register access, delays and PHY tuning tables.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>
#include <sys/i2c.h>
#include <drm/dw_hdmi.h>

#define HDMI_DESIGN_ID              0x0000
#define HDMI_REVISION_ID            0x0001
#define HDMI_PRODUCT_ID0            0x0002
#define HDMI_PRODUCT_ID1            0x0003
#define HDMI_CONFIG2_ID             0x0006
#define HDMI_IH_I2CM_STAT0          0x0105
#define HDMI_IH_I2CMPHY_STAT0       0x0108
#define HDMI_IH_MUTE_I2CM_STAT0     0x0185
#define HDMI_IH_MUTE                0x01ff
#define HDMI_TX_INVID0              0x0200
#define HDMI_TX_INSTUFFING          0x0201
#define HDMI_TX_GYDATA0             0x0202
#define HDMI_TX_GYDATA1             0x0203
#define HDMI_TX_RCRDATA0            0x0204
#define HDMI_TX_RCRDATA1            0x0205
#define HDMI_TX_BCBDATA0            0x0206
#define HDMI_TX_BCBDATA1            0x0207
#define HDMI_VP_PR_CD               0x0801
#define HDMI_VP_STUFF               0x0802
#define HDMI_VP_REMAP               0x0803
#define HDMI_VP_CONF                0x0804
#define HDMI_FC_INVIDCONF           0x1000
#define HDMI_FC_INHACTV0            0x1001
#define HDMI_FC_INHACTV1            0x1002
#define HDMI_FC_INHBLANK0           0x1003
#define HDMI_FC_INHBLANK1           0x1004
#define HDMI_FC_INVACTV0            0x1005
#define HDMI_FC_INVACTV1            0x1006
#define HDMI_FC_INVBLANK            0x1007
#define HDMI_FC_HSYNCINDELAY0       0x1008
#define HDMI_FC_HSYNCINDELAY1       0x1009
#define HDMI_FC_HSYNCINWIDTH0       0x100a
#define HDMI_FC_HSYNCINWIDTH1       0x100b
#define HDMI_FC_VSYNCINDELAY        0x100c
#define HDMI_FC_VSYNCINWIDTH        0x100d
#define HDMI_FC_CTRLDUR             0x1011
#define HDMI_FC_EXCTRLDUR           0x1012
#define HDMI_FC_EXCTRLSPAC          0x1013
#define HDMI_FC_CH0PREAM            0x1014
#define HDMI_FC_CH1PREAM            0x1015
#define HDMI_FC_CH2PREAM            0x1016
#define HDMI_PHY_CONF0              0x3000
#define HDMI_PHY_TST0               0x3001
#define HDMI_PHY_STAT0              0x3004
#define HDMI_PHY_I2CM_SLAVE         0x3020
#define HDMI_PHY_I2CM_ADDRESS       0x3021
#define HDMI_PHY_I2CM_DATAO1        0x3022
#define HDMI_PHY_I2CM_DATAO0        0x3023
#define HDMI_PHY_I2CM_OPERATION     0x3026
#define HDMI_PHY_I2CM_INT           0x3027
#define HDMI_PHY_I2CM_CTLINT        0x3028
#define HDMI_MC_CLKDIS              0x4001
#define HDMI_MC_SWRSTZ              0x4002
#define HDMI_MC_FLOWCTRL            0x4004
#define HDMI_MC_PHYRSTZ             0x4005
#define HDMI_MC_HEACPHY_RST         0x4007
#define HDMI_CSC_CFG                0x4100
#define HDMI_CSC_SCALE              0x4101
#define HDMI_A_HDCPCFG0             0x5000
#define HDMI_A_HDCPCFG1             0x5001
#define HDMI_A_VIDPOLCFG            0x5009
#define HDMI_I2CM_SLAVE             0x7e00
#define HDMI_I2CM_ADDRESS           0x7e01
#define HDMI_I2CM_DATAI             0x7e03
#define HDMI_I2CM_OPERATION         0x7e04
#define HDMI_I2CM_INT               0x7e05
#define HDMI_I2CM_CTLINT            0x7e06
#define HDMI_I2CM_DIV               0x7e07
#define HDMI_I2CM_SEGADDR           0x7e08
#define HDMI_I2CM_SOFTRSTZ          0x7e09
#define HDMI_I2CM_SEGPTR            0x7e0a

#define HDMI_PHY_PDZ                0x80
#define HDMI_PHY_ENTMDS             0x40
#define HDMI_PHY_SVSRET             0x20
#define HDMI_PHY_PDDQ               0x10
#define HDMI_PHY_TXPWRON            0x08
#define HDMI_PHY_SELDATAENPOL       0x02
#define HDMI_PHY_SELDIPIF           0x01
#define HDMI_PHY_LOCK               0x01
#define HDMI_PHY_HPD                0x02

#define HDMI_I2CM_OPERATION_READ_EXT 0x02
#define HDMI_I2CM_OPERATION_READ    0x01
#define HDMI_I2CM_STATUS_DONE       0x02
#define HDMI_I2CM_STATUS_ERROR      0x01
#define HDMI_I2CM_INT_DONE_POL      0x08
#define HDMI_I2CM_CTLINT_NACK_POL   0x80
#define HDMI_I2CM_CTLINT_ARB_POL    0x08
#define HDMI_I2CM_DDC_ADDRESS       0x50
#define HDMI_I2CM_SEGMENT_ADDRESS   0x30
#define HDMI_I2CM_TIMEOUT_US        20000u

#define HDMI_PHY_I2C_CPCE_CTRL      0x06
#define HDMI_PHY_I2C_GMPCTRL        0x15
#define HDMI_PHY_I2C_CURRCTRL       0x10
#define HDMI_PHY_I2C_PLLPHBYCTRL    0x13
#define HDMI_PHY_I2C_MSM_CTRL       0x17
#define HDMI_PHY_I2C_TXTERM         0x19
#define HDMI_PHY_I2C_CKSYMTXCTRL    0x09
#define HDMI_PHY_I2C_VLEVCTRL       0x0e
#define HDMI_PHY_I2C_CKCALCTRL      0x05

#define HDMI_PHY_I2C_DONE           0x02
#define HDMI_PHY_I2C_ERROR          0x01
#define HDMI_PHY_I2C_DONE_POL       0x08
#define HDMI_PHY_I2C_NACK_POL       0x80
#define HDMI_PHY_I2C_ARB_POL        0x08
#define HDMI_PHY_I2C_TIMEOUT_MS     1000u
#define HDMI_PHY_LOCK_TIMEOUT_MS    5u

static unsigned
dw_read(struct dw_hdmi *hdmi, unsigned reg)
{
    return (*hdmi->read)(hdmi->cookie, reg) & 0xffu;
}

static void
dw_write(struct dw_hdmi *hdmi, unsigned reg, unsigned value)
{
    (*hdmi->write)(hdmi->cookie, reg, value & 0xffu);
}

static void
dw_delay(struct dw_hdmi *hdmi, unsigned usec)
{
    (*hdmi->delay_us)(hdmi->cookie, usec);
}

static void
dw_mask(struct dw_hdmi *hdmi, unsigned reg, unsigned mask,
    unsigned value)
{
    unsigned old;

    old = dw_read(hdmi, reg);
    dw_write(hdmi, reg, (old & ~mask) | (value & mask));
}

static void
dw_ddc_init(struct dw_hdmi *hdmi)
{
    dw_write(hdmi, HDMI_I2CM_SOFTRSTZ, 0);
    dw_delay(hdmi, 10);
    dw_write(hdmi, HDMI_I2CM_DIV, 0);
    dw_write(hdmi, HDMI_I2CM_INT, HDMI_I2CM_INT_DONE_POL);
    dw_write(hdmi, HDMI_I2CM_CTLINT,
        HDMI_I2CM_CTLINT_NACK_POL | HDMI_I2CM_CTLINT_ARB_POL);
    dw_write(hdmi, HDMI_IH_I2CM_STAT0,
        HDMI_I2CM_STATUS_DONE | HDMI_I2CM_STATUS_ERROR);
    /*
     * This implementation polls IH status, so keep the CPU interrupt muted.
     * The status bits continue to latch while muted.
     */
    dw_write(hdmi, HDMI_IH_MUTE_I2CM_STAT0,
        HDMI_I2CM_STATUS_DONE | HDMI_I2CM_STATUS_ERROR);
}

static int
dw_ddc_wait(struct dw_hdmi *hdmi)
{
    unsigned elapsed;
    unsigned status;

    for (elapsed = 0; elapsed < HDMI_I2CM_TIMEOUT_US; elapsed += 10) {
        status = dw_read(hdmi, HDMI_IH_I2CM_STAT0) &
            (HDMI_I2CM_STATUS_DONE | HDMI_I2CM_STATUS_ERROR);
        if (status != 0) {
            dw_write(hdmi, HDMI_IH_I2CM_STAT0, status);
            return (status & HDMI_I2CM_STATUS_ERROR) != 0 ? EIO : 0;
        }
        dw_delay(hdmi, 10);
    }
    ++hdmi->ddc_timeouts;
    return ETIMEDOUT;
}

static int
dw_ddc_read(struct dw_hdmi *hdmi, unsigned offset, unsigned segment,
    unsigned char *data, unsigned length)
{
    unsigned operation;
    unsigned i;
    int error;

    dw_write(hdmi, HDMI_I2CM_SLAVE, HDMI_I2CM_DDC_ADDRESS);
    operation = HDMI_I2CM_OPERATION_READ;
    if (segment != 0) {
        dw_write(hdmi, HDMI_I2CM_SEGADDR, HDMI_I2CM_SEGMENT_ADDRESS);
        dw_write(hdmi, HDMI_I2CM_SEGPTR, segment);
        operation = HDMI_I2CM_OPERATION_READ_EXT;
    }
    for (i = 0; i < length; ++i) {
        dw_write(hdmi, HDMI_IH_I2CM_STAT0,
            HDMI_I2CM_STATUS_DONE | HDMI_I2CM_STATUS_ERROR);
        dw_write(hdmi, HDMI_I2CM_ADDRESS, (offset + i) & 0xffu);
        dw_write(hdmi, HDMI_I2CM_OPERATION, operation);
        error = dw_ddc_wait(hdmi);
        if (error != 0)
            return error;
        data[i] = dw_read(hdmi, HDMI_I2CM_DATAI);
    }
    return 0;
}

static int
dw_ddc_transfer(struct i2c_adapter *adapter, struct i2c_msg *messages,
    unsigned count)
{
    struct dw_hdmi *hdmi;
    unsigned offset;
    unsigned segment;
    unsigned i;
    int have_offset;
    int error;

    hdmi = (struct dw_hdmi *)adapter->cookie;
    offset = 0;
    segment = 0;
    have_offset = 0;
    error = 0;
    dw_ddc_init(hdmi);
    for (i = 0; i < count; ++i) {
        if (messages[i].addr == HDMI_I2CM_SEGMENT_ADDRESS &&
            (messages[i].flags & I2C_M_RD) == 0 &&
            messages[i].len == 1) {
            segment = messages[i].buf[0];
            continue;
        }
        if (messages[i].addr != HDMI_I2CM_DDC_ADDRESS)
            return EOPNOTSUPP;
        if ((messages[i].flags & I2C_M_RD) == 0) {
            if (messages[i].len != 1)
                return EOPNOTSUPP;
            offset = messages[i].buf[0];
            have_offset = 1;
            continue;
        }
        if (!have_offset)
            return EINVAL;
        error = dw_ddc_read(hdmi, offset, segment,
            messages[i].buf, messages[i].len);
        if (error != 0)
            break;
        offset = (offset + messages[i].len) & 0xffu;
    }
    if (error != 0 && hdmi->ddc_timeouts <= 1)
        printf("%s: DDC transfer failed offset=%x segment=%x "
            "status=%x error=%d\n", hdmi->name, offset, segment,
            dw_read(hdmi, HDMI_IH_I2CM_STAT0), error);
    return error;
}

static const struct i2c_adapter_ops dw_ddc_ops = {
    dw_ddc_transfer,
};

static int
dw_phy_i2c_write(struct dw_hdmi *hdmi, unsigned address, unsigned data)
{
    unsigned elapsed;
    unsigned status;

    dw_write(hdmi, HDMI_IH_I2CMPHY_STAT0, 0xff);
    dw_write(hdmi, HDMI_PHY_I2CM_ADDRESS, address);
    dw_write(hdmi, HDMI_PHY_I2CM_DATAO1, data >> 8);
    dw_write(hdmi, HDMI_PHY_I2CM_DATAO0, data);
    dw_write(hdmi, HDMI_PHY_I2CM_OPERATION, 0x10);
    for (elapsed = 0; elapsed < HDMI_PHY_I2C_TIMEOUT_MS; ++elapsed) {
        status = dw_read(hdmi, HDMI_IH_I2CMPHY_STAT0) &
            (HDMI_PHY_I2C_DONE | HDMI_PHY_I2C_ERROR);
        if (status != 0) {
            dw_write(hdmi, HDMI_IH_I2CMPHY_STAT0, status);
            return (status & HDMI_PHY_I2C_ERROR) != 0 ? EIO : 0;
        }
        dw_delay(hdmi, 1000);
    }
    ++hdmi->i2c_timeouts;
    return ETIMEDOUT;
}

static const struct dw_hdmi_mpll_config *
dw_mpll_config(const struct dw_hdmi *hdmi, unsigned clock)
{
    const struct dw_hdmi_mpll_config *config;

    for (config = hdmi->plat->mpll; config->max_clock_khz != 0; ++config)
        if (clock <= config->max_clock_khz)
            return config;
    return 0;
}

static const struct dw_hdmi_phy_config *
dw_phy_config(const struct dw_hdmi *hdmi, unsigned clock)
{
    const struct dw_hdmi_phy_config *config;

    for (config = hdmi->plat->phy; config->max_clock_khz != 0; ++config)
        if (clock <= config->max_clock_khz)
            return config;
    return 0;
}

static int
dw_phy_wait_lock(struct dw_hdmi *hdmi)
{
    unsigned elapsed;

    for (elapsed = 0; elapsed < HDMI_PHY_LOCK_TIMEOUT_MS; ++elapsed) {
        hdmi->phy_status = dw_read(hdmi, HDMI_PHY_STAT0);
        if ((hdmi->phy_status & HDMI_PHY_LOCK) != 0)
            return 0;
        dw_delay(hdmi, 1000);
    }
    hdmi->phy_status = dw_read(hdmi, HDMI_PHY_STAT0);
    hdmi->phy_conf = dw_read(hdmi, HDMI_PHY_CONF0);
    return ETIMEDOUT;
}

static int
dw_phy_configure(struct dw_hdmi *hdmi,
    const struct drm_display_mode *mode)
{
    const struct dw_hdmi_mpll_config *mpll;
    const struct dw_hdmi_phy_config *phy;
    int error;

    mpll = dw_mpll_config(hdmi, mode->clock_khz);
    phy = dw_phy_config(hdmi, mode->clock_khz);
    if (mpll == 0 || phy == 0)
        return EINVAL;

    dw_write(hdmi, HDMI_MC_FLOWCTRL, 0);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_TXPWRON, 0);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDDQ, HDMI_PHY_PDDQ);

    /* The Gen2 reset is active high. */
    dw_write(hdmi, HDMI_MC_PHYRSTZ, 1);
    dw_write(hdmi, HDMI_MC_PHYRSTZ, 0);
    dw_write(hdmi, HDMI_MC_HEACPHY_RST, 1);

    dw_mask(hdmi, HDMI_PHY_TST0, 0x20, 0x20);
    dw_write(hdmi, HDMI_PHY_I2CM_SLAVE, hdmi->plat->phy_i2c_addr);
    dw_mask(hdmi, HDMI_PHY_TST0, 0x20, 0);

#define DW_PHY_WRITE(address, value) do {                                \
    error = dw_phy_i2c_write(hdmi, (address), (value));                  \
    if (error != 0 && hdmi->i2c_timeouts == 1)                           \
        printf("%s: PHY I2C timeout addr=%x data=%x status=%x\n",        \
            hdmi->name, (address), (value),                              \
            dw_read(hdmi, HDMI_IH_I2CMPHY_STAT0));                       \
} while (0)

    /*
     * Linux deliberately does not fail PHY configuration if the internal
     * I2C completion interrupt is lost; the following lock test is final.
     */
    DW_PHY_WRITE(HDMI_PHY_I2C_CPCE_CTRL, mpll->cpce);
    DW_PHY_WRITE(HDMI_PHY_I2C_GMPCTRL, mpll->gmp);
    DW_PHY_WRITE(HDMI_PHY_I2C_CURRCTRL, mpll->curr);
    DW_PHY_WRITE(HDMI_PHY_I2C_PLLPHBYCTRL, 0);
    DW_PHY_WRITE(HDMI_PHY_I2C_MSM_CTRL, 0x0006);
    DW_PHY_WRITE(HDMI_PHY_I2C_TXTERM, phy->term);
    DW_PHY_WRITE(HDMI_PHY_I2C_CKSYMTXCTRL, phy->sym);
    DW_PHY_WRITE(HDMI_PHY_I2C_VLEVCTRL, phy->vlev);
    DW_PHY_WRITE(HDMI_PHY_I2C_CKCALCTRL, 0x8000);
#undef DW_PHY_WRITE

    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDZ, HDMI_PHY_PDZ);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_ENTMDS, 0);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_ENTMDS, HDMI_PHY_ENTMDS);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_TXPWRON, HDMI_PHY_TXPWRON);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDDQ, 0);
    return dw_phy_wait_lock(hdmi);
}

static int
dw_phy_init(struct dw_hdmi *hdmi, const struct drm_display_mode *mode)
{
    unsigned pass;
    int error;

    for (pass = 0; pass < 2; ++pass) {
        dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_SELDATAENPOL,
            HDMI_PHY_SELDATAENPOL);
        dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_SELDIPIF, 0);
        dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_ENTMDS, 0);
        dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDZ, 0);
        error = dw_phy_configure(hdmi, mode);
        if (error != 0)
            return error;
    }
    return 0;
}

static int
dw_fc_init(struct dw_hdmi *hdmi, const struct drm_display_mode *mode)
{
    unsigned hblank;
    unsigned vblank;
    unsigned hdelay;
    unsigned hwidth;
    unsigned vdelay;
    unsigned vwidth;
    unsigned config;

    hblank = mode->htotal - mode->hdisplay;
    vblank = mode->vtotal - mode->vdisplay;
    hdelay = mode->hsync_start - mode->hdisplay;
    hwidth = mode->hsync_end - mode->hsync_start;
    vdelay = mode->vsync_start - mode->vdisplay;
    vwidth = mode->vsync_end - mode->vsync_start;
    if (mode->hdisplay > 0xffffu || hblank > 0xffffu ||
        mode->vdisplay > 0xffffu || vblank > 0xffu ||
        hdelay > 0xffffu || hwidth > 0xffffu ||
        vdelay > 0xffu || vwidth > 0xffu)
        return EINVAL;

    config = 0x10;             /* active-high DE, progressive DVI */
    if ((mode->flags & DRM_MODE_FLAG_PVSYNC) != 0)
        config |= 0x40;
    if ((mode->flags & DRM_MODE_FLAG_PHSYNC) != 0)
        config |= 0x20;
    if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0)
        config |= 0x01;
    dw_write(hdmi, HDMI_FC_INVIDCONF, config);
    dw_write(hdmi, HDMI_FC_INHACTV0, mode->hdisplay);
    dw_write(hdmi, HDMI_FC_INHACTV1, mode->hdisplay >> 8);
    dw_write(hdmi, HDMI_FC_INHBLANK0, hblank);
    dw_write(hdmi, HDMI_FC_INHBLANK1, hblank >> 8);
    dw_write(hdmi, HDMI_FC_INVACTV0, mode->vdisplay);
    dw_write(hdmi, HDMI_FC_INVACTV1, mode->vdisplay >> 8);
    dw_write(hdmi, HDMI_FC_INVBLANK, vblank);
    dw_write(hdmi, HDMI_FC_HSYNCINDELAY0, hdelay);
    dw_write(hdmi, HDMI_FC_HSYNCINDELAY1, hdelay >> 8);
    dw_write(hdmi, HDMI_FC_HSYNCINWIDTH0, hwidth);
    dw_write(hdmi, HDMI_FC_HSYNCINWIDTH1, hwidth >> 8);
    dw_write(hdmi, HDMI_FC_VSYNCINDELAY, vdelay);
    dw_write(hdmi, HDMI_FC_VSYNCINWIDTH, vwidth);
    return 0;
}

static void
dw_enable_video_path(struct dw_hdmi *hdmi)
{
    dw_write(hdmi, HDMI_FC_CTRLDUR, 12);
    dw_write(hdmi, HDMI_FC_EXCTRLDUR, 32);
    dw_write(hdmi, HDMI_FC_EXCTRLSPAC, 1);
    dw_write(hdmi, HDMI_FC_CH0PREAM, 0x0b);
    dw_write(hdmi, HDMI_FC_CH1PREAM, 0x16);
    dw_write(hdmi, HDMI_FC_CH2PREAM, 0x21);

    /*
     * Match Linux: first enable the pixel path with TMDS held off, then
     * release TMDS.  Audio, HDCP, CEC, CSC and pixel repetition stay off.
     */
    dw_write(hdmi, HDMI_MC_CLKDIS, 0x7e);
    dw_write(hdmi, HDMI_MC_CLKDIS, 0x7c);
    dw_write(hdmi, HDMI_MC_FLOWCTRL, 0);
}

static void
dw_vp_init(struct dw_hdmi *hdmi)
{
    dw_write(hdmi, HDMI_VP_PR_CD, 0x40);
    dw_write(hdmi, HDMI_VP_STUFF, 0x27);
    dw_write(hdmi, HDMI_VP_REMAP, 0);
    dw_write(hdmi, HDMI_VP_CONF, 0x47);
}

static void
dw_tx_init(struct dw_hdmi *hdmi)
{
    dw_write(hdmi, HDMI_TX_INVID0, 0x01);
    dw_write(hdmi, HDMI_TX_INSTUFFING, 0x07);
    dw_write(hdmi, HDMI_TX_GYDATA0, 0);
    dw_write(hdmi, HDMI_TX_GYDATA1, 0);
    dw_write(hdmi, HDMI_TX_RCRDATA0, 0);
    dw_write(hdmi, HDMI_TX_RCRDATA1, 0);
    dw_write(hdmi, HDMI_TX_BCBDATA0, 0);
    dw_write(hdmi, HDMI_TX_BCBDATA1, 0);
}

static void
dw_hdcp_init(struct dw_hdmi *hdmi)
{
    dw_mask(hdmi, HDMI_A_HDCPCFG0, 0x04, 0);
    dw_mask(hdmi, HDMI_A_VIDPOLCFG, 0x10, 0x10);
    dw_mask(hdmi, HDMI_A_HDCPCFG1, 0x02, 0x02);
}

static void
dw_clear_overflow(struct dw_hdmi *hdmi)
{
    unsigned count;
    unsigned config;
    unsigned i;

    dw_write(hdmi, HDMI_MC_SWRSTZ, 0xfd);
    count = hdmi->version == 0x130au ? 4u : 1u;
    config = dw_read(hdmi, HDMI_FC_INVIDCONF);
    for (i = 0; i < count; ++i)
        dw_write(hdmi, HDMI_FC_INVIDCONF, config);
}

int
dw_hdmi_init(struct dw_hdmi *hdmi, const char *name, void *cookie,
    unsigned (*read)(void *, unsigned),
    void (*write)(void *, unsigned, unsigned),
    void (*delay_us)(void *, unsigned),
    const struct dw_hdmi_plat_data *plat)
{
    int error;

    if (hdmi == 0 || name == 0 || read == 0 || write == 0 ||
        delay_us == 0 || plat == 0 || plat->mpll == 0 || plat->phy == 0)
        return EINVAL;
    hdmi->name = name;
    hdmi->cookie = cookie;
    hdmi->read = read;
    hdmi->write = write;
    hdmi->delay_us = delay_us;
    hdmi->plat = plat;
    hdmi->version = 0;
    hdmi->phy_type = 0;
    hdmi->phy_status = 0;
    hdmi->phy_conf = 0;
    hdmi->i2c_timeouts = 0;
    hdmi->ddc_timeouts = 0;
    error = i2c_adapter_init(&hdmi->ddc, "dw-hdmi-ddc",
        &dw_ddc_ops, hdmi);
    return error;
}

int
dw_hdmi_enable(struct dw_hdmi *hdmi, const struct drm_display_mode *mode)
{
    int error;

    if (hdmi == 0 || hdmi->read == 0 || mode == 0)
        return EINVAL;
    hdmi->version = (dw_read(hdmi, HDMI_DESIGN_ID) << 8) |
        dw_read(hdmi, HDMI_REVISION_ID);
    hdmi->phy_type = dw_read(hdmi, HDMI_CONFIG2_ID);
    printf("%s: version=%x product=%x:%x phy=%x\n", hdmi->name,
        hdmi->version, dw_read(hdmi, HDMI_PRODUCT_ID0),
        dw_read(hdmi, HDMI_PRODUCT_ID1), hdmi->phy_type);

    dw_write(hdmi, HDMI_IH_MUTE, 3);
    dw_write(hdmi, HDMI_PHY_I2CM_INT, HDMI_PHY_I2C_DONE_POL);
    dw_write(hdmi, HDMI_PHY_I2CM_CTLINT,
        HDMI_PHY_I2C_NACK_POL | HDMI_PHY_I2C_ARB_POL);

    error = dw_fc_init(hdmi, mode);
    if (error != 0)
        return error;
    error = dw_phy_init(hdmi, mode);
    if (error != 0) {
        hdmi->phy_status = dw_read(hdmi, HDMI_PHY_STAT0);
        hdmi->phy_conf = dw_read(hdmi, HDMI_PHY_CONF0);
        printf("%s: PHY PLL lock failed status=%x conf=%x error=%d\n",
            hdmi->name, hdmi->phy_status, hdmi->phy_conf, error);
        return error;
    }

    dw_enable_video_path(hdmi);
    dw_vp_init(hdmi);
    dw_write(hdmi, HDMI_CSC_CFG, 0);
    dw_write(hdmi, HDMI_CSC_SCALE, 0);
    dw_tx_init(hdmi);
    dw_hdcp_init(hdmi);
    dw_clear_overflow(hdmi);
    dw_delay(hdmi, 1000);
    hdmi->phy_status = dw_read(hdmi, HDMI_PHY_STAT0);
    hdmi->phy_conf = dw_read(hdmi, HDMI_PHY_CONF0);
    return 0;
}

void
dw_hdmi_disable(struct dw_hdmi *hdmi)
{
    if (hdmi == 0 || hdmi->write == 0)
        return;
    dw_write(hdmi, HDMI_MC_CLKDIS, 0xff);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_ENTMDS, 0);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_TXPWRON, 0);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDDQ, HDMI_PHY_PDDQ);
    dw_mask(hdmi, HDMI_PHY_CONF0, HDMI_PHY_PDZ, 0);
}

int
dw_hdmi_hpd(struct dw_hdmi *hdmi)
{
    if (hdmi == 0 || hdmi->read == 0)
        return 0;
    hdmi->phy_status = dw_read(hdmi, HDMI_PHY_STAT0);
    return (hdmi->phy_status & HDMI_PHY_HPD) != 0;
}

struct i2c_adapter *
dw_hdmi_ddc_adapter(struct dw_hdmi *hdmi)
{
    if (hdmi == 0 || hdmi->ddc.ops == 0)
        return 0;
    return &hdmi->ddc;
}
