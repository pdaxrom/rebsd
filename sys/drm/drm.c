/*
 * Small static DRM/KMS core for ReBSD display drivers.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>

#define DRM_MAX_DEVICES 4

static struct drm_device *drm_devices[DRM_MAX_DEVICES];

static int
drm_mode_layout_validate(const struct drm_display_mode *mode,
    const struct drm_framebuffer *fb, int require_backing)
{
    unsigned bytes_per_pixel;

    if (mode == 0 || fb == 0 || mode->clock_khz == 0 ||
        mode->hdisplay == 0 || mode->vdisplay == 0 ||
        mode->hdisplay >= mode->hsync_start ||
        mode->hsync_start >= mode->hsync_end ||
        mode->hsync_end >= mode->htotal ||
        mode->vdisplay >= mode->vsync_start ||
        mode->vsync_start >= mode->vsync_end ||
        mode->vsync_end >= mode->vtotal)
        return EINVAL;
    if (fb->width < mode->hdisplay ||
        fb->height < mode->vdisplay || fb->bpp == 0 ||
        (fb->bpp & 7u) != 0)
        return EINVAL;
    bytes_per_pixel = fb->bpp / 8u;
    if (bytes_per_pixel == 0 ||
        fb->width > (unsigned)-1 / bytes_per_pixel ||
        fb->stride < fb->width * bytes_per_pixel ||
        fb->height > (unsigned)-1 / fb->stride ||
        fb->bytes < fb->stride * fb->height)
        return EINVAL;
    if (require_backing &&
        (fb->vaddr == 0 || fb->reserved_bytes < fb->bytes))
        return EINVAL;
    return 0;
}

int
drm_mode_validate(const struct drm_display_mode *mode,
    const struct drm_framebuffer *fb)
{
    return drm_mode_layout_validate(mode, fb, 1);
}

int
drm_device_register(struct drm_device *dev)
{
    unsigned i;
    int minor;

    if (dev == 0 || dev->name == 0 || dev->driver == 0 ||
        dev->driver->enable == 0 || dev->modes == 0 ||
        dev->mode_count == 0)
        return EINVAL;
    if (dev->registered)
        return 0;
    for (i = 0; i < dev->mode_count; ++i)
        if (drm_mode_layout_validate(&dev->modes[i].mode,
            &dev->modes[i].framebuffer,
            dev->driver->prepare_fb == 0) != 0)
            return EINVAL;
    for (minor = 0; minor < DRM_MAX_DEVICES; ++minor)
        if (drm_devices[minor] == 0)
            break;
    if (minor == DRM_MAX_DEVICES)
        return ENOSPC;

    dev->minor = minor;
    dev->primary_plane.framebuffer = 0;
    dev->primary_plane.enabled = 0;
    dev->crtc.primary = &dev->primary_plane;
    dev->crtc.enabled = 0;
    dev->encoder.crtc = &dev->crtc;
    dev->encoder.enabled = 0;
    dev->connector.encoder = &dev->encoder;
    dev->connector.status = DRM_CONNECTOR_UNKNOWN;
    dev->mode_index = 0;
    drm_devices[minor] = dev;
    dev->registered = 1;
    return 0;
}

void
drm_device_unregister(struct drm_device *dev)
{
    if (dev == 0 || !dev->registered)
        return;
    if (dev->crtc.enabled && dev->driver->disable != 0)
        (*dev->driver->disable)(dev);
    if (dev->framebuffer.vaddr != 0 &&
        dev->driver->release_fb != 0)
        (*dev->driver->release_fb)(dev, &dev->framebuffer);
    if (dev->minor >= 0 && dev->minor < DRM_MAX_DEVICES &&
        drm_devices[dev->minor] == dev)
        drm_devices[dev->minor] = 0;
    dev->primary_plane.enabled = 0;
    dev->crtc.enabled = 0;
    dev->encoder.enabled = 0;
    dev->registered = 0;
}

struct drm_device *
drm_device_lookup(unsigned minor)
{
    if (minor >= DRM_MAX_DEVICES)
        return 0;
    return drm_devices[minor];
}

int
drm_mode_set(struct drm_device *dev, const struct drm_display_mode *mode,
    struct drm_framebuffer *fb)
{
    struct drm_framebuffer candidate;
    struct drm_framebuffer old;
    int error;

    if (dev == 0 || !dev->registered)
        return ENXIO;
    candidate = *fb;
    if (dev->driver->prepare_fb != 0) {
        error = (*dev->driver->prepare_fb)(dev, mode, &candidate);
        if (error != 0)
            return error;
    }
    error = drm_mode_validate(mode, &candidate);
    if (error != 0)
        goto fail;
    error = (*dev->driver->enable)(dev, mode, &candidate);
    if (error != 0)
        goto fail;

    old = dev->framebuffer;
    dev->framebuffer = candidate;
    dev->primary_plane.framebuffer = &dev->framebuffer;
    dev->primary_plane.enabled = 1;
    dev->crtc.mode = *mode;
    dev->crtc.enabled = 1;
    dev->encoder.enabled = 1;
    dev->connector.status = DRM_CONNECTOR_CONNECTED;
    if (dev->driver->mode_changed != 0)
        (*dev->driver->mode_changed)(dev);
    if (old.vaddr != 0 && dev->driver->release_fb != 0)
        (*dev->driver->release_fb)(dev, &old);
    return 0;

fail:
    if (candidate.vaddr != fb->vaddr &&
        dev->driver->release_fb != 0)
        (*dev->driver->release_fb)(dev, &candidate);
    return error;
}

int
drm_mode_blank(struct drm_device *dev, int blank)
{
    int error;

    if (dev == 0 || !dev->registered ||
        !dev->primary_plane.enabled || dev->framebuffer.vaddr == 0)
        return ENXIO;
    if (blank) {
        if (dev->crtc.enabled && dev->driver->disable != 0)
            (*dev->driver->disable)(dev);
        dev->crtc.enabled = 0;
        dev->encoder.enabled = 0;
        return 0;
    }
    if (dev->crtc.enabled)
        return 0;
    error = (*dev->driver->enable)(dev, &dev->crtc.mode,
        &dev->framebuffer);
    if (error != 0)
        return error;
    dev->crtc.enabled = 1;
    dev->encoder.enabled = 1;
    dev->connector.status = DRM_CONNECTOR_CONNECTED;
    return 0;
}

int
drm_mode_set_index(struct drm_device *dev, unsigned index)
{
    struct drm_mode_config *config;
    int error;

    if (dev == 0 || !dev->registered)
        return ENXIO;
    if (index >= dev->mode_count)
        return EINVAL;
    if (index == dev->mode_index && dev->crtc.enabled)
        return 0;
    config = &dev->modes[index];
    error = drm_mode_set(dev, &config->mode, &config->framebuffer);
    if (error == 0)
        dev->mode_index = index;
    return error;
}
