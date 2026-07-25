/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Architecture-independent EDID reader and display-mode parser.
 */

#ifndef _DRM_EDID_H_
#define _DRM_EDID_H_

#include <sys/drm.h>
#include <sys/i2c.h>

#define DRM_EDID_BLOCK_BYTES        128u
#define DRM_EDID_MAX_BLOCKS         4u
#define DRM_EDID_MAX_BYTES          \
    (DRM_EDID_BLOCK_BYTES * DRM_EDID_MAX_BLOCKS)
#define DRM_EDID_MAX_MODES          24u
#define DRM_EDID_NO_PREFERRED       ((unsigned)-1)

struct drm_edid_info {
    char vendor[4];
    char monitor_name[14];
    unsigned product;
    unsigned width_mm;
    unsigned height_mm;
    unsigned extension_count;
    unsigned preferred_mode;
};

int drm_edid_read(struct i2c_adapter *, unsigned char *, unsigned,
    unsigned *);
int drm_edid_parse(const unsigned char *, unsigned, struct drm_edid_info *,
    struct drm_display_mode *, unsigned, unsigned *);

#endif
