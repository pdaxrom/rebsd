/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Small allocation-free EDID parser for the ReBSD DRM core.  It handles
 * base-block detailed timings, CTA video data blocks and CTA detailed
 * timings.  Common established and standard timings are used as fallbacks.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>
#include <sys/i2c.h>
#include <drm/drm_edid.h>

#define EDID_DDC_ADDRESS            0x50u
#define EDID_SEGMENT_ADDRESS        0x30u
#define EDID_HEADER_BYTES           8u
#define EDID_BASE_DTD_OFFSET        54u
#define EDID_DTD_BYTES              18u
#define EDID_BASE_DTD_COUNT         4u
#define EDID_EXTENSION_COUNT        126u
#define EDID_CHECKSUM               127u
#define EDID_CTA_TAG                0x02u
#define EDID_CTA_VIDEO_BLOCK        2u

#define MODE(clock, hd, hss, hse, ht, vd, vss, vse, vt, mode_flags) \
    { (clock), (hd), (hss), (hse), (ht), (vd), (vss), (vse), (vt), \
      (mode_flags) }

struct edid_vic_mode {
    unsigned vic;
    struct drm_display_mode mode;
};

struct edid_standard_mode {
    unsigned width;
    unsigned height;
    unsigned refresh;
    struct drm_display_mode mode;
};

struct edid_established_mode {
    unsigned byte;
    unsigned mask;
    struct drm_display_mode mode;
};

static const unsigned char edid_header[EDID_HEADER_BYTES] =
    { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

static const struct edid_vic_mode edid_vic_modes[] = {
    { 1, MODE(25175, 640, 656, 752, 800,
        480, 490, 492, 525,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 2, MODE(27000, 720, 736, 798, 858,
        480, 489, 495, 525,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 3, MODE(27000, 720, 736, 798, 858,
        480, 489, 495, 525,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 4, MODE(74250, 1280, 1390, 1430, 1650,
        720, 725, 730, 750,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 16, MODE(148500, 1920, 2008, 2052, 2200,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 17, MODE(27000, 720, 732, 796, 864,
        576, 581, 586, 625,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 18, MODE(27000, 720, 732, 796, 864,
        576, 581, 586, 625,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 19, MODE(74250, 1280, 1720, 1760, 1980,
        720, 725, 730, 750,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 31, MODE(148500, 1920, 2448, 2492, 2640,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 32, MODE(74250, 1920, 2558, 2602, 2750,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 33, MODE(74250, 1920, 2448, 2492, 2640,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 34, MODE(74250, 1920, 2008, 2052, 2200,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static const struct edid_standard_mode edid_standard_modes[] = {
    { 640, 480, 60, MODE(25175, 640, 656, 752, 800,
        480, 490, 492, 525,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 800, 600, 60, MODE(40000, 800, 840, 968, 1056,
        600, 601, 605, 628,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 1024, 768, 60, MODE(65000, 1024, 1048, 1184, 1344,
        768, 771, 777, 806,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 1280, 1024, 60, MODE(108000, 1280, 1328, 1440, 1688,
        1024, 1025, 1028, 1066,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 1360, 768, 60, MODE(85500, 1360, 1424, 1536, 1792,
        768, 771, 777, 795,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 1440, 900, 60, MODE(106500, 1440, 1520, 1672, 1904,
        900, 903, 909, 934,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 1680, 1050, 60, MODE(146250, 1680, 1784, 1960, 2240,
        1050, 1053, 1059, 1089,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 1920, 1080, 60, MODE(148500, 1920, 2008, 2052, 2200,
        1080, 1084, 1089, 1125,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static const struct edid_established_mode edid_established_modes[] = {
    { 35, 0x20, MODE(25175, 640, 656, 752, 800,
        480, 490, 492, 525,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 35, 0x01, MODE(40000, 800, 840, 968, 1056,
        600, 601, 605, 628,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    { 36, 0x04, MODE(65000, 1024, 1048, 1184, 1344,
        768, 771, 777, 806,
        DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    { 37, 0x80, MODE(135000, 1280, 1296, 1440, 1688,
        1024, 1025, 1028, 1066,
        DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static int
edid_block_valid(const unsigned char *block, unsigned block_number)
{
    unsigned checksum;
    unsigned i;

    if (block_number == 0)
        for (i = 0; i < EDID_HEADER_BYTES; ++i)
            if (block[i] != edid_header[i])
                return 0;
    checksum = 0;
    for (i = 0; i < DRM_EDID_BLOCK_BYTES; ++i)
        checksum += block[i];
    return (checksum & 0xffu) == 0;
}

static int
edid_read_block(struct i2c_adapter *adapter, unsigned block_number,
    unsigned char *data)
{
    struct i2c_msg messages[3];
    unsigned char offset;
    unsigned char segment;
    unsigned count;

    offset = (block_number & 1u) != 0 ? 128u : 0u;
    segment = block_number >> 1;
    count = 0;
    if (segment != 0) {
        messages[count].addr = EDID_SEGMENT_ADDRESS;
        messages[count].flags = 0;
        messages[count].len = 1;
        messages[count].buf = &segment;
        ++count;
    }
    messages[count].addr = EDID_DDC_ADDRESS;
    messages[count].flags = 0;
    messages[count].len = 1;
    messages[count].buf = &offset;
    ++count;
    messages[count].addr = EDID_DDC_ADDRESS;
    messages[count].flags = I2C_M_RD;
    messages[count].len = DRM_EDID_BLOCK_BYTES;
    messages[count].buf = data;
    ++count;
    return i2c_transfer(adapter, messages, count);
}

int
drm_edid_read(struct i2c_adapter *adapter, unsigned char *edid,
    unsigned capacity, unsigned *bytes_read)
{
    unsigned blocks;
    unsigned block;
    int error;

    if (adapter == 0 || edid == 0 || bytes_read == 0 ||
        capacity < DRM_EDID_BLOCK_BYTES)
        return EINVAL;
    *bytes_read = 0;
    error = edid_read_block(adapter, 0, edid);
    if (error != 0)
        return error;
    if (!edid_block_valid(edid, 0))
        return EINVAL;
    *bytes_read = DRM_EDID_BLOCK_BYTES;
    blocks = 1u + edid[EDID_EXTENSION_COUNT];
    if (blocks > capacity / DRM_EDID_BLOCK_BYTES)
        blocks = capacity / DRM_EDID_BLOCK_BYTES;
    for (block = 1; block < blocks; ++block) {
        error = edid_read_block(adapter, block,
            edid + block * DRM_EDID_BLOCK_BYTES);
        if (error != 0)
            break;
        *bytes_read += DRM_EDID_BLOCK_BYTES;
    }
    return 0;
}

static int
edid_mode_equal(const struct drm_display_mode *a,
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
edid_add_mode(struct drm_display_mode *modes, unsigned capacity,
    unsigned *count, const struct drm_display_mode *mode, unsigned *index)
{
    unsigned i;

    for (i = 0; i < *count; ++i)
        if (edid_mode_equal(&modes[i], mode)) {
            if (index != 0)
                *index = i;
            return 0;
        }
    if (*count >= capacity)
        return ENOSPC;
    modes[*count] = *mode;
    if (index != 0)
        *index = *count;
    ++*count;
    return 0;
}

static int
edid_detailed_mode(const unsigned char *dtd, struct drm_display_mode *mode)
{
    unsigned hactive;
    unsigned hblank;
    unsigned hsync_offset;
    unsigned hsync_width;
    unsigned vactive;
    unsigned vblank;
    unsigned vsync_offset;
    unsigned vsync_width;
    unsigned misc;

    mode->clock_khz = ((unsigned)dtd[1] << 8 | dtd[0]) * 10u;
    if (mode->clock_khz == 0)
        return EINVAL;
    hactive = ((unsigned)dtd[4] & 0xf0u) << 4 | dtd[2];
    hblank = ((unsigned)dtd[4] & 0x0fu) << 8 | dtd[3];
    vactive = ((unsigned)dtd[7] & 0xf0u) << 4 | dtd[5];
    vblank = ((unsigned)dtd[7] & 0x0fu) << 8 | dtd[6];
    hsync_offset = ((unsigned)dtd[11] & 0xc0u) << 2 | dtd[8];
    hsync_width = ((unsigned)dtd[11] & 0x30u) << 4 | dtd[9];
    vsync_offset = ((unsigned)dtd[11] & 0x0cu) << 2 |
        (dtd[10] >> 4);
    vsync_width = ((unsigned)dtd[11] & 0x03u) << 4 |
        (dtd[10] & 0x0fu);
    if (hactive < 64 || vactive < 64 || hblank == 0 || vblank == 0 ||
        hsync_width == 0 || vsync_width == 0)
        return EINVAL;
    mode->hdisplay = hactive;
    mode->hsync_start = hactive + hsync_offset;
    mode->hsync_end = mode->hsync_start + hsync_width;
    mode->htotal = hactive + hblank;
    mode->vdisplay = vactive;
    mode->vsync_start = vactive + vsync_offset;
    mode->vsync_end = mode->vsync_start + vsync_width;
    mode->vtotal = vactive + vblank;
    if (mode->hsync_end > mode->htotal ||
        mode->vsync_end > mode->vtotal)
        return EINVAL;
    misc = dtd[17];
    mode->flags = 0;
    if ((misc & 0x18u) == 0x18u) {
        mode->flags |= (misc & 0x02u) != 0 ?
            DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
        mode->flags |= (misc & 0x04u) != 0 ?
            DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
    } else {
        mode->flags |= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
    }
    if ((misc & 0x80u) != 0)
        mode->flags |= DRM_MODE_FLAG_INTERLACE;
    return 0;
}

static void
edid_parse_dtds(const unsigned char *data, unsigned count,
    struct drm_display_mode *modes, unsigned capacity, unsigned *mode_count,
    unsigned *preferred, int first_is_preferred)
{
    struct drm_display_mode mode;
    unsigned index;
    unsigned i;

    for (i = 0; i < count; ++i) {
        bzero(&mode, sizeof(mode));
        if (edid_detailed_mode(data + i * EDID_DTD_BYTES, &mode) != 0)
            continue;
        if (edid_add_mode(modes, capacity, mode_count, &mode, &index) != 0)
            return;
        if (i == 0 && first_is_preferred &&
            *preferred == DRM_EDID_NO_PREFERRED)
            *preferred = index;
    }
}

static const struct drm_display_mode *
edid_vic_mode(unsigned vic)
{
    unsigned i;

    for (i = 0; i < sizeof(edid_vic_modes) / sizeof(edid_vic_modes[0]); ++i)
        if (edid_vic_modes[i].vic == vic)
            return &edid_vic_modes[i].mode;
    return 0;
}

static void
edid_parse_cta(const unsigned char *cta, struct drm_display_mode *modes,
    unsigned capacity, unsigned *mode_count, unsigned *preferred)
{
    const struct drm_display_mode *mode;
    unsigned dtd_offset;
    unsigned end;
    unsigned index;
    unsigned length;
    unsigned native;
    unsigned offset;
    unsigned tag;
    unsigned vic;
    unsigned i;

    dtd_offset = cta[2];
    if (dtd_offset == 0 || dtd_offset > EDID_CHECKSUM)
        dtd_offset = EDID_CHECKSUM;
    offset = 4;
    while (offset < dtd_offset) {
        tag = cta[offset] >> 5;
        length = cta[offset] & 0x1fu;
        ++offset;
        if (length > dtd_offset - offset)
            break;
        if (tag == EDID_CTA_VIDEO_BLOCK)
            for (i = 0; i < length; ++i) {
                native = cta[offset + i] & 0x80u;
                vic = cta[offset + i] & 0x7fu;
                mode = edid_vic_mode(vic);
                if (mode == 0)
                    continue;
                if (edid_add_mode(modes, capacity, mode_count, mode,
                    &index) != 0)
                    return;
                if (native != 0 && *preferred == DRM_EDID_NO_PREFERRED)
                    *preferred = index;
            }
        offset += length;
    }
    if (dtd_offset >= EDID_CHECKSUM)
        return;
    end = EDID_CHECKSUM - dtd_offset;
    edid_parse_dtds(cta + dtd_offset, end / EDID_DTD_BYTES,
        modes, capacity, mode_count, preferred, 0);
}

static void
edid_parse_standard(const unsigned char *edid,
    struct drm_display_mode *modes, unsigned capacity, unsigned *mode_count)
{
    const struct edid_standard_mode *standard;
    unsigned aspect;
    unsigned height;
    unsigned refresh;
    unsigned width;
    unsigned i;
    unsigned j;

    for (i = 0; i < 8; ++i) {
        if (edid[38 + i * 2] == 0x01 &&
            edid[39 + i * 2] == 0x01)
            continue;
        width = ((unsigned)edid[38 + i * 2] + 31u) * 8u;
        aspect = edid[39 + i * 2] >> 6;
        if (aspect == 0)
            height = edid[19] < 3 ? width : width * 10u / 16u;
        else if (aspect == 1)
            height = width * 3u / 4u;
        else if (aspect == 2)
            height = width * 4u / 5u;
        else
            height = width * 9u / 16u;
        refresh = (edid[39 + i * 2] & 0x3fu) + 60u;
        for (j = 0; j < sizeof(edid_standard_modes) /
            sizeof(edid_standard_modes[0]); ++j) {
            standard = &edid_standard_modes[j];
            if (standard->width == width && standard->height == height &&
                standard->refresh == refresh) {
                if (edid_add_mode(modes, capacity, mode_count,
                    &standard->mode, 0) != 0)
                    return;
                break;
            }
        }
    }
}

static void
edid_parse_established(const unsigned char *edid,
    struct drm_display_mode *modes, unsigned capacity, unsigned *mode_count)
{
    const struct edid_established_mode *established;
    unsigned i;

    for (i = 0; i < sizeof(edid_established_modes) /
        sizeof(edid_established_modes[0]); ++i) {
        established = &edid_established_modes[i];
        if ((edid[established->byte] & established->mask) == 0)
            continue;
        if (edid_add_mode(modes, capacity, mode_count,
            &established->mode, 0) != 0)
            return;
    }
}

static void
edid_parse_name(const unsigned char *edid, char name[14])
{
    const unsigned char *descriptor;
    unsigned length;
    unsigned slot;

    name[0] = '\0';
    for (slot = 0; slot < EDID_BASE_DTD_COUNT; ++slot) {
        descriptor = edid + EDID_BASE_DTD_OFFSET +
            slot * EDID_DTD_BYTES;
        if (descriptor[0] != 0 || descriptor[1] != 0 ||
            descriptor[2] != 0 || descriptor[3] != 0xfcu)
            continue;
        for (length = 0; length < 13; ++length) {
            if (descriptor[5 + length] == '\n' ||
                descriptor[5 + length] == '\r')
                break;
            name[length] = descriptor[5 + length];
        }
        while (length != 0 && name[length - 1] == ' ')
            --length;
        name[length] = '\0';
        return;
    }
}

int
drm_edid_parse(const unsigned char *edid, unsigned bytes,
    struct drm_edid_info *info, struct drm_display_mode *modes,
    unsigned capacity, unsigned *mode_count)
{
    const unsigned char *extension;
    unsigned blocks;
    unsigned block;

    if (edid == 0 || info == 0 || modes == 0 || mode_count == 0 ||
        bytes < DRM_EDID_BLOCK_BYTES || capacity == 0 ||
        !edid_block_valid(edid, 0))
        return EINVAL;
    bzero(info, sizeof(*info));
    *mode_count = 0;
    info->vendor[0] = ((edid[8] >> 2) & 0x1fu) + '@';
    info->vendor[1] = (((edid[8] & 0x03u) << 3) |
        (edid[9] >> 5)) + '@';
    info->vendor[2] = (edid[9] & 0x1fu) + '@';
    info->vendor[3] = '\0';
    info->product = edid[10] | (unsigned)edid[11] << 8;
    info->width_mm = (unsigned)edid[21] * 10u;
    info->height_mm = (unsigned)edid[22] * 10u;
    info->extension_count = edid[EDID_EXTENSION_COUNT];
    info->preferred_mode = DRM_EDID_NO_PREFERRED;
    edid_parse_name(edid, info->monitor_name);

    edid_parse_dtds(edid + EDID_BASE_DTD_OFFSET, EDID_BASE_DTD_COUNT,
        modes, capacity, mode_count, &info->preferred_mode, 1);
    blocks = bytes / DRM_EDID_BLOCK_BYTES;
    for (block = 1; block < blocks; ++block) {
        extension = edid + block * DRM_EDID_BLOCK_BYTES;
        if (!edid_block_valid(extension, block) ||
            extension[0] != EDID_CTA_TAG)
            continue;
        edid_parse_cta(extension, modes, capacity, mode_count,
            &info->preferred_mode);
    }
    edid_parse_standard(edid, modes, capacity, mode_count);
    edid_parse_established(edid, modes, capacity, mode_count);
    return *mode_count != 0 ? 0 : ENOENT;
}
