#ifndef BACKEND_DRM_IFACE_H
#define BACKEND_DRM_IFACE_H

#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct wlr_drm_backend;
struct wlr_drm_connector;
struct wlr_drm_crtc;

// Used to provide atomic or legacy DRM functions
struct wlr_drm_interface {
	// Commit al pending changes on a CRTC.
	bool (*crtc_commit)(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags);
};

extern const struct wlr_drm_interface atomic_iface;
extern const struct wlr_drm_interface legacy_iface;

bool drm_legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
	struct wlr_drm_crtc *crtc, size_t size, uint16_t *lut);

#endif
