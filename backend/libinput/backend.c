#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/signal.h"

static struct wlr_libinput_backend *get_libinput_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_libinput(wlr_backend));
	return (struct wlr_libinput_backend *)wlr_backend;
}

static int libinput_open_restricted(const char *path,
		int flags, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	return wlr_session_open_file(backend->session, path);
}

static void libinput_close_restricted(int fd, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	wlr_session_close_file(backend->session, fd);
}

static const struct libinput_interface libinput_impl = {
	.open_restricted = libinput_open_restricted,
	.close_restricted = libinput_close_restricted
};

static int handle_libinput_readable(int fd, uint32_t mask, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	if (libinput_dispatch(backend->libinput_context) != 0) {
		wlr_log(WLR_ERROR, "Failed to dispatch libinput");
		// TODO: some kind of abort?
		return 0;
	}
	struct libinput_event *event;
	while ((event = libinput_get_event(backend->libinput_context))) {
		handle_libinput_event(backend, event);
		libinput_event_destroy(event);
	}
	return 0;
}

static void log_libinput(struct libinput *libinput_context,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	_wlr_vlog(WLR_ERROR, fmt, args);
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_libinput_backend *backend =
		get_libinput_backend_from_backend(wlr_backend);
	wlr_log(WLR_DEBUG, "Initializing libinput");

	backend->libinput_context = libinput_udev_create_context(&libinput_impl,
		backend, backend->session->udev);
	if (!backend->libinput_context) {
		wlr_log(WLR_ERROR, "Failed to create libinput context");
		return false;
	}

	if (libinput_udev_assign_seat(backend->libinput_context,
			backend->session->seat) != 0) {
		wlr_log(WLR_ERROR, "Failed to assign libinput seat");
		return false;
	}

	// TODO: More sophisticated logging
	libinput_log_set_handler(backend->libinput_context, log_libinput);
	libinput_log_set_priority(backend->libinput_context, LIBINPUT_LOG_PRIORITY_ERROR);

	int libinput_fd = libinput_get_fd(backend->libinput_context);
	char *no_devs = getenv("WLR_LIBINPUT_NO_DEVICES");
	if (no_devs) {
		if (strcmp(no_devs, "1") != 0) {
			no_devs = NULL;
		}
	}
	if (!no_devs && backend->wlr_device_lists.length == 0) {
		handle_libinput_readable(libinput_fd, WL_EVENT_READABLE, backend);
		if (backend->wlr_device_lists.length == 0) {
			wlr_log(WLR_ERROR, "libinput initialization failed, no input devices");
			wlr_log(WLR_ERROR, "Set WLR_LIBINPUT_NO_DEVICES=1 to suppress this check");
			return false;
		}
	}

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(backend->display);
	if (backend->input_event) {
		wl_event_source_remove(backend->input_event);
	}
	backend->input_event = wl_event_loop_add_fd(event_loop, libinput_fd,
			WL_EVENT_READABLE, handle_libinput_readable, backend);
	if (!backend->input_event) {
		wlr_log(WLR_ERROR, "Failed to create input event on event loop");
		return false;
	}
	wlr_log(WLR_DEBUG, "libinput successfully initialized");
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	if (!wlr_backend) {
		return;
	}
	struct wlr_libinput_backend *backend =
		get_libinput_backend_from_backend(wlr_backend);

	for (size_t i = 0; i < backend->wlr_device_lists.length; i++) {
		struct wl_list *wlr_devices = backend->wlr_device_lists.items[i];
		struct wlr_input_device *wlr_dev, *next;
		wl_list_for_each_safe(wlr_dev, next, wlr_devices, link) {
			wlr_input_device_destroy(wlr_dev);
		}
		free(wlr_devices);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, wlr_backend);

	wl_list_remove(&backend->display_destroy.link);
	wl_list_remove(&backend->session_destroy.link);
	wl_list_remove(&backend->session_signal.link);

	wlr_list_finish(&backend->wlr_device_lists);
	if (backend->input_event) {
		wl_event_source_remove(backend->input_event);
	}
	libinput_unref(backend->libinput_context);
	free(backend);
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
};

bool wlr_backend_is_libinput(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_libinput_backend *backend =
		wl_container_of(listener, backend, session_signal);
	struct wlr_session *session = data;

	if (!backend->libinput_context) {
		return;
	}

	if (session->active) {
		libinput_resume(backend->libinput_context);
	} else {
		libinput_suspend(backend->libinput_context);
	}
}

static void handle_session_destroy(struct wl_listener *listener, void *data) {
	struct wlr_libinput_backend *backend =
		wl_container_of(listener, backend, session_destroy);
	backend_destroy(&backend->backend);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_libinput_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_libinput_backend_create(struct wl_display *display,
		struct wlr_session *session) {
	struct wlr_libinput_backend *backend =
		calloc(1, sizeof(struct wlr_libinput_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);

	if (!wlr_list_init(&backend->wlr_device_lists)) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		goto error_backend;
	}

	backend->session = session;
	backend->display = display;

	backend->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	backend->session_destroy.notify = handle_session_destroy;
	wl_signal_add(&session->events.destroy, &backend->session_destroy);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->backend;
error_backend:
	free(backend);
	return NULL;
}

struct libinput_device *wlr_libinput_get_device_handle(
		struct wlr_input_device *wlr_dev) {
	struct wlr_libinput_input_device *dev =
		(struct wlr_libinput_input_device *)wlr_dev;
	return dev->handle;
}

uint32_t usec_to_msec(uint64_t usec) {
	return (uint32_t)(usec / 1000);
}
