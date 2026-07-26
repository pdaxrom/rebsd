/*
 * Synopsys DesignWare HDMI bridge interface.
 */

#ifndef _DRM_DW_HDMI_H_
#define _DRM_DW_HDMI_H_

#include <sys/drm.h>
#include <sys/i2c.h>

struct dw_hdmi_mpll_config {
    unsigned max_clock_khz;
    unsigned cpce;
    unsigned gmp;
    unsigned curr;
};

struct dw_hdmi_phy_config {
    unsigned max_clock_khz;
    unsigned term;
    unsigned sym;
    unsigned vlev;
};

struct dw_hdmi_plat_data {
    const struct dw_hdmi_mpll_config *mpll;
    const struct dw_hdmi_phy_config *phy;
    unsigned phy_i2c_addr;
};

struct dw_hdmi {
    const char *name;
    void *cookie;
    unsigned (*read)(void *, unsigned);
    void (*write)(void *, unsigned, unsigned);
    void (*delay_us)(void *, unsigned);
    const struct dw_hdmi_plat_data *plat;
    unsigned version;
    unsigned phy_type;
    unsigned phy_status;
    unsigned phy_conf;
    unsigned i2c_timeouts;
    unsigned ddc_timeouts;
    struct i2c_adapter ddc;
};

int dw_hdmi_init(struct dw_hdmi *, const char *, void *,
    unsigned (*)(void *, unsigned),
    void (*)(void *, unsigned, unsigned),
    void (*)(void *, unsigned),
    const struct dw_hdmi_plat_data *);
int dw_hdmi_enable(struct dw_hdmi *, const struct drm_display_mode *);
void dw_hdmi_disable(struct dw_hdmi *);
int dw_hdmi_hpd(struct dw_hdmi *);
struct i2c_adapter *dw_hdmi_ddc_adapter(struct dw_hdmi *);

#endif /* _DRM_DW_HDMI_H_ */
