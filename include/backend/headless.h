#ifndef BACKEND_HEADLESS_H
#define BACKEND_HEADLESS_H

#include <wlr/backend/headless.h>
#include <wlr/backend/interface.h>
#include <wlr/render/gles2.h>

#define HEADLESS_DEFAULT_REFRESH (60 * 1000) // 60 Hz

struct wlr_headless_backend {
	struct wlr_backend backend;
	struct wlr_egl priv_egl; // may be uninitialized
	struct wlr_egl *egl;
	struct wlr_renderer *renderer;
	struct wl_display *display;
	struct wl_list outputs;
	size_t last_output_num;
	struct wl_list input_devices;
	struct wl_listener display_destroy;
	struct wl_listener renderer_destroy;
	bool started;
	GLenum internal_format;
};

struct wlr_headless_output {
	struct wlr_output wlr_output;

	struct wlr_headless_backend *backend;
	struct wl_list link;

	GLuint fbo, rbo;

	struct wl_event_source *frame_timer;
	int frame_delay; // ms
};

struct wlr_headless_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_headless_backend *backend;
};

struct wlr_headless_backend *headless_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
