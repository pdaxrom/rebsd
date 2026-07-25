/*
 * Architecture-independent dumb framebuffer character device for ReBSD DRM.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/drm.h>
#include <linux/fb.h>
#include <vm/vm_param.h>

typedef char drmfb_linux_fix_abi_size[
    sizeof(struct fb_fix_screeninfo) == 68 ? 1 : -1];
typedef char drmfb_linux_var_abi_size[
    sizeof(struct fb_var_screeninfo) == 160 ? 1 : -1];

static struct drm_device *
drmfb_device(dev_t dev)
{
    struct drm_device *drm;

    drm = drm_device_lookup(minor(dev));
    if (drm == 0 || !drm->registered ||
        !drm->primary_plane.enabled || drm->framebuffer.vaddr == 0)
        return 0;
    return drm;
}

int
drmfb_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return drmfb_device(dev) != 0 ? 0 : ENXIO;
}

int
drmfb_close(dev_t dev, int flag, int mode)
{
    (void)dev;
    (void)flag;
    (void)mode;
    return 0;
}

static int
drmfb_rw(dev_t dev, struct uio *uio)
{
    struct drm_device *drm;
    volatile unsigned char *fb;
    unsigned offset;
    unsigned count;
    int error;

    if (uio->uio_offset < 0)
        return EINVAL;
    drm = drmfb_device(dev);
    if (drm == 0)
        return ENXIO;
    fb = drm->framebuffer.vaddr;
    while (uio->uio_resid != 0) {
        offset = (unsigned)uio->uio_offset;
        if ((off_t)offset != uio->uio_offset ||
            offset >= drm->framebuffer.bytes)
            return 0;
        count = uio->uio_resid;
        if (count > drm->framebuffer.bytes - offset)
            count = drm->framebuffer.bytes - offset;
        error = uiomove((caddr_t)fb + offset, count, uio);
        if (error != 0)
            return error;
    }
    return 0;
}

int
drmfb_read(dev_t dev, struct uio *uio, int flag)
{
    (void)flag;
    return drmfb_rw(dev, uio);
}

int
drmfb_write(dev_t dev, struct uio *uio, int flag)
{
    (void)flag;
    return drmfb_rw(dev, uio);
}

static void
drmfb_linux_id(char id[16], const char *name)
{
    unsigned i;

    for (i = 0; i < 15 && name[i] != '\0'; ++i)
        id[i] = name[i];
    id[i] = '\0';
}

static int
drmfb_linux_var(const struct drm_display_mode *mode,
    const struct drm_framebuffer *fb, struct fb_var_screeninfo *var)
{
    bzero(var, sizeof(*var));
    var->xres = mode->hdisplay;
    var->yres = mode->vdisplay;
    var->xres_virtual = fb->width;
    var->yres_virtual = fb->height;
    var->bits_per_pixel = fb->bpp;
    switch (fb->format) {
    case DRM_FORMAT_XRGB8888:
        var->red.offset = 16;
        var->red.length = 8;
        var->green.offset = 8;
        var->green.length = 8;
        var->blue.length = 8;
        var->transp.offset = 24;
        break;
    case DRM_FORMAT_RGBA5551:
        var->red.offset = 11;
        var->red.length = 5;
        var->green.offset = 6;
        var->green.length = 5;
        var->blue.offset = 1;
        var->blue.length = 5;
        var->transp.length = 1;
        break;
    case DRM_FORMAT_RGBA8888:
        var->red.offset = 24;
        var->red.length = 8;
        var->green.offset = 16;
        var->green.length = 8;
        var->blue.offset = 8;
        var->blue.length = 8;
        var->transp.length = 8;
        break;
    default:
        return EINVAL;
    }
    var->height = (__u32)-1;
    var->width = (__u32)-1;
    var->pixclock = KHZ2PICOS(mode->clock_khz);
    var->right_margin = mode->hsync_start - mode->hdisplay;
    var->hsync_len = mode->hsync_end - mode->hsync_start;
    var->left_margin = mode->htotal - mode->hsync_end;
    var->lower_margin = mode->vsync_start - mode->vdisplay;
    var->vsync_len = mode->vsync_end - mode->vsync_start;
    var->upper_margin = mode->vtotal - mode->vsync_end;
    if (mode->flags & DRM_MODE_FLAG_PHSYNC)
        var->sync |= FB_SYNC_HOR_HIGH_ACT;
    if (mode->flags & DRM_MODE_FLAG_PVSYNC)
        var->sync |= FB_SYNC_VERT_HIGH_ACT;
    if (mode->flags & DRM_MODE_FLAG_INTERLACE)
        var->vmode = FB_VMODE_INTERLACED;
    return 0;
}

static void
drmfb_linux_fix(const struct drm_device *drm,
    struct fb_fix_screeninfo *fix)
{
    const struct drm_framebuffer *fb = &drm->framebuffer;

    bzero(fix, sizeof(*fix));
    drmfb_linux_id(fix->id, drm->name);
    fix->smem_start = fb->paddr;
    fix->smem_len = fb->bytes;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->visual = FB_VISUAL_TRUECOLOR;
    fix->line_length = fb->stride;
    fix->accel = FB_ACCEL_NONE;
}

static int
drmfb_linux_mode_index(const struct drm_device *drm,
    const struct fb_var_screeninfo *var, unsigned *index)
{
    const struct drm_mode_config *config;
    unsigned i;

    if (var->grayscale != 0 || var->nonstd != 0 ||
        var->xoffset != 0 || var->yoffset != 0 ||
        var->rotate != FB_ROTATE_UR)
        return EINVAL;
    for (i = 0; i < drm->mode_count; ++i) {
        config = &drm->modes[i];
        if (var->xres != config->mode.hdisplay ||
            var->yres != config->mode.vdisplay ||
            var->bits_per_pixel != config->framebuffer.bpp)
            continue;
        if (var->xres_virtual != 0 &&
            var->xres_virtual != config->framebuffer.width)
            continue;
        if (var->yres_virtual != 0 &&
            var->yres_virtual != config->framebuffer.height)
            continue;
        *index = i;
        return 0;
    }
    return EINVAL;
}

static int
drmfb_linux_get_fix(struct drm_device *drm, caddr_t data)
{
    struct fb_fix_screeninfo fix;

    drmfb_linux_fix(drm, &fix);
    return copyout((caddr_t)&fix, data, sizeof(fix));
}

static int
drmfb_linux_get_var(struct drm_device *drm, caddr_t data)
{
    struct fb_var_screeninfo var;
    int error;

    error = drmfb_linux_var(&drm->crtc.mode, &drm->framebuffer, &var);
    if (error != 0)
        return error;
    return copyout((caddr_t)&var, data, sizeof(var));
}

static int
drmfb_linux_put_var(struct drm_device *drm, caddr_t data)
{
    struct fb_var_screeninfo var;
    unsigned index;
    int error;

    error = copyin(data, (caddr_t)&var, sizeof(var));
    if (error != 0)
        return error;
    if ((var.activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW &&
        (var.activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_TEST)
        return EINVAL;
    error = drmfb_linux_mode_index(drm, &var, &index);
    if (error != 0)
        return error;
    if ((var.activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_TEST) {
        error = drm_mode_set_index(drm, index);
        if (error != 0)
            return error;
    }
    error = drmfb_linux_var(&drm->modes[index].mode,
        &drm->modes[index].framebuffer, &var);
    if (error != 0)
        return error;
    return copyout((caddr_t)&var, data, sizeof(var));
}

static int
drmfb_linux_pan(struct drm_device *drm, caddr_t data)
{
    struct fb_var_screeninfo var;
    int error;

    error = copyin(data, (caddr_t)&var, sizeof(var));
    if (error != 0)
        return error;
    if (var.xoffset != 0 || var.yoffset != 0 ||
        (var.vmode & FB_VMODE_YWRAP) != 0)
        return EINVAL;
    return drmfb_linux_get_var(drm, data);
}

int
drmfb_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct drm_device *drm;
    struct drm_framebuffer *fb;
    struct drmfb_info *info;
    struct drmfb_map *map;
    struct drmfb_mode *mode;
    struct drm_mode_config *config;
    vm_size_t mapped;

    (void)flag;
    drm = drmfb_device(dev);
    if (drm == 0)
        return ENXIO;
    fb = &drm->framebuffer;
    switch (cmd) {
    case DRMFBIOC_GETINFO:
        info = (struct drmfb_info *)data;
        info->width = fb->width;
        info->height = fb->height;
        info->stride = fb->stride;
        info->bpp = fb->bpp;
        info->format = fb->format;
        info->fb_phys = fb->paddr;
        info->fb_bytes = fb->bytes;
        info->reserved_bytes = fb->reserved_bytes;
        info->pixel_clock_khz = drm->crtc.mode.clock_khz;
        info->phy_status = drm->phy_status;
        info->mode_index = drm->mode_index;
        info->mode_count = drm->mode_count;
        return 0;
    case DRMFBIOC_GETMAP:
        map = (struct drmfb_map *)data;
        if (vm_size_round_page(fb->bytes, &mapped) != 0 ||
            mapped > fb->reserved_bytes)
            return EINVAL;
        map->vaddr = fb->map_hint;
        map->bytes = mapped;
        map->reserved_bytes = fb->reserved_bytes;
        return 0;
    case DRMFBIOC_GETMODE:
        mode = (struct drmfb_mode *)data;
        if (mode->index >= drm->mode_count)
            return EINVAL;
        config = &drm->modes[mode->index];
        mode->width = config->framebuffer.width;
        mode->height = config->framebuffer.height;
        mode->stride = config->framebuffer.stride;
        mode->bpp = config->framebuffer.bpp;
        mode->format = config->framebuffer.format;
        mode->pixel_clock_khz = config->mode.clock_khz;
        mode->flags = config->mode.flags;
        return 0;
    case DRMFBIOC_SETMODE:
        mode = (struct drmfb_mode *)data;
        return drm_mode_set_index(drm, mode->index);
    case FBIOGET_FSCREENINFO:
        return drmfb_linux_get_fix(drm, data);
    case FBIOGET_VSCREENINFO:
        return drmfb_linux_get_var(drm, data);
    case FBIOPUT_VSCREENINFO:
        return drmfb_linux_put_var(drm, data);
    case FBIOPAN_DISPLAY:
        return drmfb_linux_pan(drm, data);
    case FBIOBLANK:
        if ((u_int)data > FB_BLANK_POWERDOWN)
            return EINVAL;
        return drm_mode_blank(drm,
            (u_int)data != FB_BLANK_UNBLANK);
    case FBIOGETCMAP:
    case FBIOPUTCMAP:
        return EINVAL;
    default:
        return ENOTTY;
    }
}

int
drmfb_mmap(dev_t dev, off_t offset, u_int size, int protection,
    u_int *paddr, int *cache)
{
    struct drm_device *drm;
    struct drm_framebuffer *fb;
    vm_size_t mapped;

    drm = drmfb_device(dev);
    if (drm == 0)
        return ENXIO;
    fb = &drm->framebuffer;
    if (offset < 0 || (off_t)(u_int)offset != offset ||
        ((unsigned)offset & VM_PAGE_MASK) != 0 || size == 0 ||
        !vm_size_page_aligned((vm_size_t)size) ||
        (protection & VM_PROT_EXECUTE) != 0 || paddr == 0 || cache == 0)
        return EINVAL;
    if (vm_size_round_page(fb->bytes, &mapped) != 0 ||
        (unsigned)offset > mapped || size > mapped - (unsigned)offset)
        return EINVAL;
    *paddr = fb->paddr + (unsigned)offset;
    *cache = fb->cache_mode;
    return 0;
}
