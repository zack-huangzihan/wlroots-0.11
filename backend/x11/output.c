#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "util/signal.h"

static int signal_frame(void *data) {
	struct wlr_x11_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

static void parse_xcb_setup(struct wlr_output *output,
		xcb_connection_t *xcb) {
	const xcb_setup_t *xcb_setup = xcb_get_setup(xcb);

	snprintf(output->make, sizeof(output->make), "%.*s",
			xcb_setup_vendor_length(xcb_setup),
			xcb_setup_vendor(xcb_setup));
	snprintf(output->model, sizeof(output->model), "%"PRIu16".%"PRIu16,
			xcb_setup->protocol_major_version,
			xcb_setup->protocol_minor_version);
}

static struct wlr_x11_output *get_x11_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_x11(wlr_output));
	return (struct wlr_x11_output *)wlr_output;
}

static void output_set_refresh(struct wlr_output *wlr_output, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = X11_DEFAULT_REFRESH;
	}

	wlr_output_update_custom_mode(&output->wlr_output, wlr_output->width,
		wlr_output->height, refresh);

	output->frame_delay = 1000000 / refresh;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	output_set_refresh(&output->wlr_output, refresh);

	const uint32_t values[] = { width, height };
	xcb_void_cookie_t cookie = xcb_configure_window_checked(
		x11->xcb, output->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

	xcb_generic_error_t *error;
	if ((error = xcb_request_check(x11->xcb, cookie))) {
		wlr_log(WLR_ERROR, "Could not set window size to %dx%d\n",
			width, height);
		free(error);
		return false;
	}

	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	wlr_input_device_destroy(&output->pointer_dev);
	wlr_input_device_destroy(&output->touch_dev);

	wl_list_remove(&output->link);
	wl_event_source_remove(output->frame_timer);
	wlr_egl_destroy_surface(&x11->egl, output->surf);
	xcb_destroy_window(x11->xcb, output->win);
	xcb_flush(x11->xcb);
	free(output);
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	return wlr_egl_make_current(&x11->egl, output->surf, buffer_age);
}

static bool output_test(struct wlr_output *wlr_output) {
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable an X11 output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	if (!output_test(wlr_output)) {
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		if (!output_set_custom_mode(wlr_output,
				wlr_output->pending.custom_mode.width,
				wlr_output->pending.custom_mode.height,
				wlr_output->pending.custom_mode.refresh)) {
			return false;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED &&
			x11->atoms.variable_refresh != XCB_ATOM_NONE) {
		if (wlr_output->pending.adaptive_sync_enabled) {
			uint32_t enabled = 1;
			xcb_change_property(x11->xcb, XCB_PROP_MODE_REPLACE, output->win,
				x11->atoms.variable_refresh, XCB_ATOM_CARDINAL, 32, 1,
				&enabled);
			wlr_output->adaptive_sync_status = WLR_OUTPUT_ADAPTIVE_SYNC_UNKNOWN;
		} else {
			xcb_delete_property(x11->xcb, output->win,
				x11->atoms.variable_refresh);
			wlr_output->adaptive_sync_status = WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		pixman_region32_t *damage = NULL;
		if (wlr_output->pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
			damage = &wlr_output->pending.damage;
		}

		if (!wlr_egl_swap_buffers(&x11->egl, output->surf, damage)) {
			return false;
		}

		wlr_output_send_present(wlr_output, NULL);
	}

	return true;
}

static void output_rollback_render(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	wlr_egl_unset_current(&output->x11->egl);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.test = output_test,
	.commit = output_commit,
	.rollback_render = output_rollback_render,
};

struct wlr_output *wlr_x11_output_create(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);

	if (!x11->started) {
		++x11->requested_outputs;
		return NULL;
	}

	struct wlr_x11_output *output = calloc(1, sizeof(struct wlr_x11_output));
	if (output == NULL) {
		return NULL;
	}
	output->x11 = x11;

	struct wlr_output *wlr_output = &output->wlr_output;
	wlr_output_init(wlr_output, &x11->backend, &output_impl, x11->wl_display);

	wlr_output->width = 1024;
	wlr_output->height = 768;

	output_set_refresh(&output->wlr_output, 0);

	snprintf(wlr_output->name, sizeof(wlr_output->name), "X11-%zd",
		++x11->last_output_num);
	parse_xcb_setup(wlr_output, x11->xcb);

	char description[128];
	snprintf(description, sizeof(description),
		"X11 output %zd", x11->last_output_num);
	wlr_output_set_description(wlr_output, description);

	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};
	output->win = xcb_generate_id(x11->xcb);
	xcb_create_window(x11->xcb, XCB_COPY_FROM_PARENT, output->win,
		x11->screen->root, 0, 0, wlr_output->width, wlr_output->height, 1,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, x11->screen->root_visual, mask, values);

	struct {
		xcb_input_event_mask_t head;
		xcb_input_xi_event_mask_t mask;
	} xinput_mask = {
		.head = { .deviceid = XCB_INPUT_DEVICE_ALL_MASTER, .mask_len = 1 },
		.mask = XCB_INPUT_XI_EVENT_MASK_KEY_PRESS |
			XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_MOTION |
			XCB_INPUT_XI_EVENT_MASK_ENTER |
			XCB_INPUT_XI_EVENT_MASK_LEAVE |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_END |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE,
	};
	xcb_input_xi_select_events(x11->xcb, output->win, 1, &xinput_mask.head);

	output->surf = wlr_egl_create_surface(&x11->egl, &output->win);
	if (!output->surf) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		free(output);
		return NULL;
	}

	xcb_change_property(x11->xcb, XCB_PROP_MODE_REPLACE, output->win,
		x11->atoms.wm_protocols, XCB_ATOM_ATOM, 32, 1,
		&x11->atoms.wm_delete_window);

	wlr_x11_output_set_title(wlr_output, NULL);

	xcb_map_window(x11->xcb, output->win);
	xcb_flush(x11->xcb);

	struct wl_event_loop *ev = wl_display_get_event_loop(x11->wl_display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&x11->outputs, &output->link);

	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	wlr_output_update_enabled(wlr_output, true);

	wlr_input_device_init(&output->pointer_dev, WLR_INPUT_DEVICE_POINTER,
		&input_device_impl, "X11 pointer", 0, 0);
	wlr_pointer_init(&output->pointer, &pointer_impl);
	output->pointer_dev.pointer = &output->pointer;
	output->pointer_dev.output_name = strdup(wlr_output->name);

	wlr_input_device_init(&output->touch_dev, WLR_INPUT_DEVICE_TOUCH,
		&input_device_impl, "X11 touch", 0, 0);
	wlr_touch_init(&output->touch, &touch_impl);
	output->touch_dev.touch = &output->touch;
	output->touch_dev.output_name = strdup(wlr_output->name);
	wl_list_init(&output->touchpoints);

	wlr_signal_emit_safe(&x11->backend.events.new_output, wlr_output);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->pointer_dev);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->touch_dev);

	return wlr_output;
}

void handle_x11_configure_notify(struct wlr_x11_output *output,
		xcb_configure_notify_event_t *ev) {
	// ignore events that set an invalid size:
	if (ev->width > 0 && ev->height > 0) {
		wlr_output_update_custom_mode(&output->wlr_output, ev->width,
			ev->height, output->wlr_output.refresh);

		// Move the pointer to its new location
		update_x11_pointer_position(output, output->x11->time);
	} else {
		wlr_log(WLR_DEBUG,
			"Ignoring X11 configure event for height=%d, width=%d",
			ev->width, ev->height);
	}
}

bool wlr_output_is_x11(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

void wlr_x11_output_set_title(struct wlr_output *output, const char *title) {
	struct wlr_x11_output *x11_output = get_x11_output_from_output(output);

	char wl_title[32];
	if (title == NULL) {
		if (snprintf(wl_title, sizeof(wl_title), "wlroots - %s", output->name) <= 0) {
			return;
		}
		title = wl_title;
	}

	xcb_change_property(x11_output->x11->xcb, XCB_PROP_MODE_REPLACE, x11_output->win,
		x11_output->x11->atoms.net_wm_name, x11_output->x11->atoms.utf8_string, 8,
		strlen(title), title);
}
