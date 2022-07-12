#include <assert.h>
#include <drm_fourcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

static bool egl_get_config(EGLDisplay disp, const EGLint *attribs,
		EGLConfig *out, EGLint visual_id) {
	EGLint count = 0, matched = 0, ret;

	ret = eglGetConfigs(disp, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		wlr_log(WLR_ERROR, "eglGetConfigs returned no configs");
		return false;
	}

	EGLConfig configs[count];

	ret = eglChooseConfig(disp, attribs, configs, count, &matched);
	if (ret == EGL_FALSE) {
		wlr_log(WLR_ERROR, "eglChooseConfig failed");
		return false;
	}

	for (int i = 0; i < matched; ++i) {
		EGLint visual;
		if (!eglGetConfigAttrib(disp, configs[i],
				EGL_NATIVE_VISUAL_ID, &visual)) {
			continue;
		}

		if (!visual_id || visual == visual_id) {
			*out = configs[i];
			return true;
		}
	}

	wlr_log(WLR_ERROR, "no valid egl config found");
	return false;
}

static enum wlr_log_importance egl_log_importance_to_wlr(EGLint type) {
	switch (type) {
	case EGL_DEBUG_MSG_CRITICAL_KHR: return WLR_ERROR;
	case EGL_DEBUG_MSG_ERROR_KHR:    return WLR_ERROR;
	case EGL_DEBUG_MSG_WARN_KHR:     return WLR_ERROR;
	case EGL_DEBUG_MSG_INFO_KHR:     return WLR_INFO;
	default:                         return WLR_INFO;
	}
}

static const char *egl_error_str(EGLint error) {
	switch (error) {
	case EGL_SUCCESS:
		return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED:
		return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS:
		return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC:
		return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE:
		return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT:
		return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG:
		return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE:
		return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY:
		return "EGL_BAD_DISPLAY";
	case EGL_BAD_SURFACE:
		return "EGL_BAD_SURFACE";
	case EGL_BAD_MATCH:
		return "EGL_BAD_MATCH";
	case EGL_BAD_PARAMETER:
		return "EGL_BAD_PARAMETER";
	case EGL_BAD_NATIVE_PIXMAP:
		return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW:
		return "EGL_BAD_NATIVE_WINDOW";
	case EGL_CONTEXT_LOST:
		return "EGL_CONTEXT_LOST";
	}
	return "unknown error";
}

static void egl_log(EGLenum error, const char *command, EGLint msg_type,
		EGLLabelKHR thread, EGLLabelKHR obj, const char *msg) {
	_wlr_log(egl_log_importance_to_wlr(msg_type),
		"[EGL] command: %s, error: %s (0x%x), message: \"%s\"",
		command, egl_error_str(error), error, msg);
}

static bool check_egl_ext(const char *exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = exts + strlen(exts);

	while (exts < end) {
		if (*exts == ' ') {
			exts++;
			continue;
		}
		size_t n = strcspn(exts, " ");
		if (n == extlen && strncmp(ext, exts, n) == 0) {
			return true;
		}
		exts += n;
	}
	return false;
}

static void load_egl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

static int get_egl_dmabuf_formats(struct wlr_egl *egl, int **formats);
static int get_egl_dmabuf_modifiers(struct wlr_egl *egl, int format,
	uint64_t **modifiers, EGLBoolean **external_only);

static void init_dmabuf_formats(struct wlr_egl *egl) {
	int *formats;
	int formats_len = get_egl_dmabuf_formats(egl, &formats);
	if (formats_len < 0) {
		return;
	}

	egl->external_only_dmabuf_formats = calloc(formats_len, sizeof(EGLBoolean *));
	if (egl->external_only_dmabuf_formats == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto out;
	}

	size_t external_formats_len = 0;
	for (int i = 0; i < formats_len; i++) {
		uint32_t fmt = formats[i];

		uint64_t *modifiers;
		EGLBoolean *external_only;
		int modifiers_len =
			get_egl_dmabuf_modifiers(egl, fmt, &modifiers, &external_only);
		if (modifiers_len < 0) {
			continue;
		}

		if (modifiers_len == 0) {
			wlr_drm_format_set_add(&egl->dmabuf_formats, fmt, DRM_FORMAT_MOD_INVALID);
		}

		for (int j = 0; j < modifiers_len; j++) {
			wlr_drm_format_set_add(&egl->dmabuf_formats, fmt, modifiers[j]);
		}

		free(modifiers);

		egl->external_only_dmabuf_formats[external_formats_len] = external_only;
		external_formats_len++;
	}

	char *str_formats = malloc(formats_len * 5 + 1);
	if (str_formats == NULL) {
		goto out;
	}
	for (int i = 0; i < formats_len; i++) {
		snprintf(&str_formats[i*5], (formats_len - i) * 5 + 1, "%.4s ",
			(char*)&formats[i]);
	}
	wlr_log(WLR_DEBUG, "Supported dmabuf buffer formats: %s", str_formats);
	free(str_formats);

out:
	free(formats);
}

bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
		const EGLint *config_attribs, EGLint visual_id) {
	const char *client_exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (client_exts_str == NULL) {
		if (eglGetError() == EGL_BAD_DISPLAY) {
			wlr_log(WLR_ERROR, "EGL_EXT_client_extensions not supported");
		} else {
			wlr_log(WLR_ERROR, "Failed to query EGL client extensions");
		}
		return false;
	}

	if (!check_egl_ext(client_exts_str, "EGL_EXT_platform_base")) {
		wlr_log(WLR_ERROR, "EGL_EXT_platform_base not supported");
		return false;
	}
	load_egl_proc(&egl->procs.eglGetPlatformDisplayEXT,
		"eglGetPlatformDisplayEXT");
	load_egl_proc(&egl->procs.eglCreatePlatformWindowSurfaceEXT,
		"eglCreatePlatformWindowSurfaceEXT");

	if (check_egl_ext(client_exts_str, "EGL_KHR_debug")) {
		load_egl_proc(&egl->procs.eglDebugMessageControlKHR,
			"eglDebugMessageControlKHR");

		static const EGLAttrib debug_attribs[] = {
			EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE,
			EGL_NONE,
		};
		egl->procs.eglDebugMessageControlKHR(egl_log, debug_attribs);
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to bind to the OpenGL ES API");
		goto error;
	}

	egl->display = egl->procs.eglGetPlatformDisplayEXT(platform,
		remote_display, NULL);
	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(WLR_ERROR, "Failed to create EGL display");
		goto error;
	}

	egl->platform = platform;

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to initialize EGL");
		goto error;
	}

	const char *display_exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (display_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to query EGL display extensions");
		return false;
	}

	if (check_egl_ext(display_exts_str, "EGL_KHR_image_base")) {
		egl->exts.image_base_khr = true;
		load_egl_proc(&egl->procs.eglCreateImageKHR, "eglCreateImageKHR");
		load_egl_proc(&egl->procs.eglDestroyImageKHR, "eglDestroyImageKHR");
	}

	egl->exts.buffer_age_ext =
		check_egl_ext(display_exts_str, "EGL_EXT_buffer_age");

	if (check_egl_ext(display_exts_str, "EGL_KHR_swap_buffers_with_damage")) {
		egl->exts.swap_buffers_with_damage = true;
		load_egl_proc(&egl->procs.eglSwapBuffersWithDamage,
			"eglSwapBuffersWithDamageKHR");
	} else if (check_egl_ext(display_exts_str,
			"EGL_EXT_swap_buffers_with_damage")) {
		egl->exts.swap_buffers_with_damage = true;
		load_egl_proc(&egl->procs.eglSwapBuffersWithDamage,
			"eglSwapBuffersWithDamageEXT");
	}

	egl->exts.image_dmabuf_import_ext =
		check_egl_ext(display_exts_str, "EGL_EXT_image_dma_buf_import");
	if (check_egl_ext(display_exts_str,
			"EGL_EXT_image_dma_buf_import_modifiers")) {
		egl->exts.image_dmabuf_import_modifiers_ext = true;
		load_egl_proc(&egl->procs.eglQueryDmaBufFormatsEXT,
			"eglQueryDmaBufFormatsEXT");
		load_egl_proc(&egl->procs.eglQueryDmaBufModifiersEXT,
			"eglQueryDmaBufModifiersEXT");
	}

	if (check_egl_ext(display_exts_str, "EGL_MESA_image_dma_buf_export")) {
		egl->exts.image_dma_buf_export_mesa = true;
		load_egl_proc(&egl->procs.eglExportDMABUFImageQueryMESA,
			"eglExportDMABUFImageQueryMESA");
		load_egl_proc(&egl->procs.eglExportDMABUFImageMESA,
			"eglExportDMABUFImageMESA");
	}

	if (check_egl_ext(display_exts_str, "EGL_WL_bind_wayland_display")) {
		egl->exts.bind_wayland_display_wl = true;
		load_egl_proc(&egl->procs.eglBindWaylandDisplayWL,
			"eglBindWaylandDisplayWL");
		load_egl_proc(&egl->procs.eglUnbindWaylandDisplayWL,
			"eglUnbindWaylandDisplayWL");
		load_egl_proc(&egl->procs.eglQueryWaylandBufferWL,
			"eglQueryWaylandBufferWL");
	}

	if (!egl_get_config(egl->display, config_attribs, &egl->config, visual_id)) {
		wlr_log(WLR_ERROR, "Failed to get EGL config");
		goto error;
	}

	wlr_log(WLR_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(WLR_INFO, "Supported EGL client extensions: %s", client_exts_str);
	wlr_log(WLR_INFO, "Supported EGL display extensions: %s", display_exts_str);
	wlr_log(WLR_INFO, "EGL vendor: %s", eglQueryString(egl->display, EGL_VENDOR));

	init_dmabuf_formats(egl);

	bool ext_context_priority =
		check_egl_ext(display_exts_str, "EGL_IMG_context_priority");

	size_t atti = 0;
	EGLint attribs[5];
	attribs[atti++] = EGL_CONTEXT_CLIENT_VERSION;
	attribs[atti++] = 2;

	// On DRM, request a high priority context if possible
	bool request_high_priority = ext_context_priority &&
		platform == EGL_PLATFORM_GBM_MESA;

	// Try to reschedule all of our rendering to be completed first. If it
	// fails, it will fallback to the default priority (MEDIUM).
	if (request_high_priority) {
		attribs[atti++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		attribs[atti++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}

	attribs[atti++] = EGL_NONE;
	assert(atti <= sizeof(attribs)/sizeof(attribs[0]));

	egl->context = eglCreateContext(egl->display, egl->config,
		EGL_NO_CONTEXT, attribs);
	if (egl->context == EGL_NO_CONTEXT) {
		wlr_log(WLR_ERROR, "Failed to create EGL context");
		goto error;
	}

	if (request_high_priority) {
		EGLint priority = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
		eglQueryContext(egl->display, egl->context,
			EGL_CONTEXT_PRIORITY_LEVEL_IMG, &priority);
		if (priority != EGL_CONTEXT_PRIORITY_HIGH_IMG) {
			wlr_log(WLR_INFO, "Failed to obtain a high priority context");
		} else {
			wlr_log(WLR_DEBUG, "Obtained high priority context");
		}
	}

	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->display) {
		eglTerminate(egl->display);
	}
	eglReleaseThread();
	return false;
}

void wlr_egl_finish(struct wlr_egl *egl) {
	if (egl == NULL) {
		return;
	}

	for (size_t i = 0; i < egl->dmabuf_formats.len; i++) {
		free(egl->external_only_dmabuf_formats[i]);
	}
	free(egl->external_only_dmabuf_formats);

	wlr_drm_format_set_finish(&egl->dmabuf_formats);

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display) {
		assert(egl->exts.bind_wayland_display_wl);
		egl->procs.eglUnbindWaylandDisplayWL(egl->display, egl->wl_display);
	}

	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();
}

bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display) {
	if (!egl->exts.bind_wayland_display_wl) {
		return false;
	}

	if (egl->procs.eglBindWaylandDisplayWL(egl->display, local_display)) {
		egl->wl_display = local_display;
		return true;
	}

	return false;
}

bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImage image) {
	if (!egl->exts.image_base_khr) {
		return false;
	}
	if (!image) {
		return true;
	}
	return egl->procs.eglDestroyImageKHR(egl->display, image);
}

EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window) {
	assert(egl->procs.eglCreatePlatformWindowSurfaceEXT);
	EGLSurface surf = egl->procs.eglCreatePlatformWindowSurfaceEXT(
		egl->display, egl->config, window, NULL);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		return EGL_NO_SURFACE;
	}
	return surf;
}

static int egl_get_buffer_age(struct wlr_egl *egl, EGLSurface surface) {
	if (!egl->exts.buffer_age_ext) {
		return -1;
	}

	EGLint buffer_age;
	EGLBoolean ok = eglQuerySurface(egl->display, surface,
		EGL_BUFFER_AGE_EXT, &buffer_age);
	if (!ok) {
		wlr_log(WLR_ERROR, "Failed to get EGL surface buffer age");
		return -1;
	}

	return buffer_age;
}

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
		int *buffer_age) {
	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		wlr_log(WLR_ERROR, "eglMakeCurrent failed");
		return false;
	}

	if (buffer_age != NULL) {
		*buffer_age = egl_get_buffer_age(egl, surface);
	}
	return true;
}

bool wlr_egl_unset_current(struct wlr_egl *egl) {
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT)) {
		wlr_log(WLR_ERROR, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

bool wlr_egl_is_current(struct wlr_egl *egl) {
	return eglGetCurrentContext() == egl->context;
}

void wlr_egl_save_context(struct wlr_egl_context *context) {
	context->display = eglGetCurrentDisplay();
	context->context = eglGetCurrentContext();
	context->draw_surface = eglGetCurrentSurface(EGL_DRAW);
	context->read_surface = eglGetCurrentSurface(EGL_READ);
}

bool wlr_egl_restore_context(struct wlr_egl_context *context) {
	// If the saved context is a null-context, we must use the current
	// display instead of the saved display because eglMakeCurrent() can't
	// handle EGL_NO_DISPLAY.
	EGLDisplay display = context->display == EGL_NO_DISPLAY ?
		eglGetCurrentDisplay() : context->display;

	// If the current display is also EGL_NO_DISPLAY, we assume that there
	// is currently no context set and no action needs to be taken to unset
	// the context.
	if (display == EGL_NO_DISPLAY) {
		return true;
	}

	return eglMakeCurrent(display, context->draw_surface,
			context->read_surface, context->context);
}

bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
		pixman_region32_t *damage) {
	// Never block when swapping buffers on Wayland
	if (egl->platform == EGL_PLATFORM_WAYLAND_EXT) {
		eglSwapInterval(egl->display, 0);
	}

	EGLBoolean ret;
	if (damage != NULL && egl->exts.swap_buffers_with_damage) {
		EGLint width = 0, height = 0;
		eglQuerySurface(egl->display, surface, EGL_WIDTH, &width);
		eglQuerySurface(egl->display, surface, EGL_HEIGHT, &height);

		pixman_region32_t flipped_damage;
		pixman_region32_init(&flipped_damage);
		wlr_region_transform(&flipped_damage, damage,
			WL_OUTPUT_TRANSFORM_FLIPPED_180, width, height);

		int nrects;
		pixman_box32_t *rects =
			pixman_region32_rectangles(&flipped_damage, &nrects);
		EGLint egl_damage[4 * nrects + 1];
		for (int i = 0; i < nrects; ++i) {
			egl_damage[4*i] = rects[i].x1;
			egl_damage[4*i + 1] = rects[i].y1;
			egl_damage[4*i + 2] = rects[i].x2 - rects[i].x1;
			egl_damage[4*i + 3] = rects[i].y2 - rects[i].y1;
		}

		pixman_region32_fini(&flipped_damage);

		if (nrects == 0) {
			// Swapping with no rects is the same as swapping with the entire
			// surface damaged. To swap with no damage, we set the damage region
			// to a single empty rectangle.
			nrects = 1;
			memset(egl_damage, 0, sizeof(egl_damage));
		}

		ret = egl->procs.eglSwapBuffersWithDamage(egl->display, surface,
			egl_damage, nrects);
	} else {
		ret = eglSwapBuffers(egl->display, surface);
	}

	if (!ret) {
		wlr_log(WLR_ERROR, "eglSwapBuffers failed");
		return false;
	}

	wlr_egl_unset_current(egl);
	return true;
}

EGLImageKHR wlr_egl_create_image_from_wl_drm(struct wlr_egl *egl,
		struct wl_resource *data, EGLint *fmt, int *width, int *height,
		bool *inverted_y) {
	if (!egl->exts.bind_wayland_display_wl || !egl->exts.image_base_khr) {
		return NULL;
	}

	if (!egl->procs.eglQueryWaylandBufferWL(egl->display, data,
			EGL_TEXTURE_FORMAT, fmt)) {
		return NULL;
	}

	egl->procs.eglQueryWaylandBufferWL(egl->display, data, EGL_WIDTH, width);
	egl->procs.eglQueryWaylandBufferWL(egl->display, data, EGL_HEIGHT, height);

	EGLint _inverted_y;
	if (egl->procs.eglQueryWaylandBufferWL(egl->display, data,
			EGL_WAYLAND_Y_INVERTED_WL, &_inverted_y)) {
		*inverted_y = !!_inverted_y;
	} else {
		*inverted_y = false;
	}

	const EGLint attribs[] = {
		EGL_WAYLAND_PLANE_WL, 0,
		EGL_NONE,
	};
	return egl->procs.eglCreateImageKHR(egl->display, egl->context,
		EGL_WAYLAND_BUFFER_WL, data, attribs);
}

static bool dmabuf_format_is_external_only(struct wlr_egl *egl,
		uint32_t format, uint64_t modifier) {
	for (size_t i = 0; i < egl->dmabuf_formats.len; i++) {
		struct wlr_drm_format *fmt = egl->dmabuf_formats.formats[i];
		if (fmt->format == format) {
			if (egl->external_only_dmabuf_formats[i] == NULL) {
				break;
			}
			for (size_t j = 0; j < fmt->len; j++) {
				if (fmt->modifiers[j] == modifier) {
					return egl->external_only_dmabuf_formats[i][j];
				}
			}
			break;
		}
	}
	// No info, we're doomed. Choose GL_TEXTURE_EXTERNAL_OES and hope for the
	// best.
	return true;
}

EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_attributes *attributes, bool *external_only) {
	if (!egl->exts.image_base_khr || !egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_ERROR, "dmabuf import extension not present");
		return NULL;
	}

	bool has_modifier = false;

	// we assume the same way we assumed formats without the import_modifiers
	// extension that mod_linear is supported. The special mod mod_invalid
	// is sometimes used to signal modifier unawareness which is what we
	// have here
	if (attributes->modifier != DRM_FORMAT_MOD_INVALID &&
			attributes->modifier != DRM_FORMAT_MOD_LINEAR) {
		if (!egl->exts.image_dmabuf_import_modifiers_ext) {
			wlr_log(WLR_ERROR, "dmabuf modifiers extension not present");
			return NULL;
		}
		has_modifier = true;
	}

	unsigned int atti = 0;
	EGLint attribs[50];
	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = attributes->width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = attributes->height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = attributes->format;

	struct {
		EGLint fd;
		EGLint offset;
		EGLint pitch;
		EGLint mod_lo;
		EGLint mod_hi;
	} attr_names[WLR_DMABUF_MAX_PLANES] = {
		{
			EGL_DMA_BUF_PLANE0_FD_EXT,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT,
			EGL_DMA_BUF_PLANE0_PITCH_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE1_FD_EXT,
			EGL_DMA_BUF_PLANE1_OFFSET_EXT,
			EGL_DMA_BUF_PLANE1_PITCH_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE2_FD_EXT,
			EGL_DMA_BUF_PLANE2_OFFSET_EXT,
			EGL_DMA_BUF_PLANE2_PITCH_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE3_FD_EXT,
			EGL_DMA_BUF_PLANE3_OFFSET_EXT,
			EGL_DMA_BUF_PLANE3_PITCH_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
		}
	};

	for (int i=0; i < attributes->n_planes; i++) {
		attribs[atti++] = attr_names[i].fd;
		attribs[atti++] = attributes->fd[i];
		attribs[atti++] = attr_names[i].offset;
		attribs[atti++] = attributes->offset[i];
		attribs[atti++] = attr_names[i].pitch;
		attribs[atti++] = attributes->stride[i];
		if (has_modifier) {
			attribs[atti++] = attr_names[i].mod_lo;
			attribs[atti++] = attributes->modifier & 0xFFFFFFFF;
			attribs[atti++] = attr_names[i].mod_hi;
			attribs[atti++] = attributes->modifier >> 32;
		}
	}
	attribs[atti++] = EGL_NONE;
	assert(atti < sizeof(attribs)/sizeof(attribs[0]));

	EGLImageKHR image = egl->procs.eglCreateImageKHR(egl->display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (image == EGL_NO_IMAGE_KHR) {
		wlr_log(WLR_ERROR, "eglCreateImageKHR failed");
		return EGL_NO_IMAGE_KHR;
	}

	*external_only = dmabuf_format_is_external_only(egl,
		attributes->format, attributes->modifier);
	return image;
}

static int get_egl_dmabuf_formats(struct wlr_egl *egl, int **formats) {
	if (!egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_DEBUG, "DMA-BUF import extension not present");
		return -1;
	}

	// when we only have the image_dmabuf_import extension we can't query
	// which formats are supported. These two are on almost always
	// supported; it's the intended way to just try to create buffers.
	// Just a guess but better than not supporting dmabufs at all,
	// given that the modifiers extension isn't supported everywhere.
	if (!egl->exts.image_dmabuf_import_modifiers_ext) {
		static const int fallback_formats[] = {
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_XRGB8888,
		};
		static unsigned num = sizeof(fallback_formats) /
			sizeof(fallback_formats[0]);

		*formats = calloc(num, sizeof(int));
		if (!*formats) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return -1;
		}

		memcpy(*formats, fallback_formats, num * sizeof(**formats));
		return num;
	}

	EGLint num;
	if (!egl->procs.eglQueryDmaBufFormatsEXT(egl->display, 0, NULL, &num)) {
		wlr_log(WLR_ERROR, "Failed to query number of dmabuf formats");
		return -1;
	}

	*formats = calloc(num, sizeof(int));
	if (*formats == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		return -1;
	}

	if (!egl->procs.eglQueryDmaBufFormatsEXT(egl->display, num, *formats, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf format");
		free(*formats);
		return -1;
	}
	return num;
}

static int get_egl_dmabuf_modifiers(struct wlr_egl *egl, int format,
		uint64_t **modifiers, EGLBoolean **external_only) {
	*modifiers = NULL;
	*external_only = NULL;

	if (!egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_DEBUG, "DMA-BUF extension not present");
		return -1;
	}
	if (!egl->exts.image_dmabuf_import_modifiers_ext) {
		return 0;
	}

	EGLint num;
	if (!egl->procs.eglQueryDmaBufModifiersEXT(egl->display, format, 0,
			NULL, NULL, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf number of modifiers");
		return -1;
	}
	if (num == 0) {
		return 0;
	}

	*modifiers = calloc(num, sizeof(uint64_t));
	if (*modifiers == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return -1;
	}
	*external_only = calloc(num, sizeof(EGLBoolean));
	if (*external_only == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(*modifiers);
		*modifiers = NULL;
		return -1;
	}

	if (!egl->procs.eglQueryDmaBufModifiersEXT(egl->display, format, num,
			*modifiers, *external_only, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf modifiers");
		free(*modifiers);
		return -1;
	}
	return num;
}

const struct wlr_drm_format_set *wlr_egl_get_dmabuf_formats(struct wlr_egl *egl) {
	return &egl->dmabuf_formats;
}

bool wlr_egl_export_image_to_dmabuf(struct wlr_egl *egl, EGLImageKHR image,
		int32_t width, int32_t height, uint32_t flags,
		struct wlr_dmabuf_attributes *attribs) {
	memset(attribs, 0, sizeof(struct wlr_dmabuf_attributes));

	if (!egl->exts.image_dma_buf_export_mesa) {
		return false;
	}

	// Only one set of modifiers is returned for all planes
	if (!egl->procs.eglExportDMABUFImageQueryMESA(egl->display, image,
			(int *)&attribs->format, &attribs->n_planes, &attribs->modifier)) {
		return false;
	}
	if (attribs->n_planes > WLR_DMABUF_MAX_PLANES) {
		wlr_log(WLR_ERROR, "EGL returned %d planes, but only %d are supported",
			attribs->n_planes, WLR_DMABUF_MAX_PLANES);
		return false;
	}

	if (!egl->procs.eglExportDMABUFImageMESA(egl->display, image, attribs->fd,
			(EGLint *)attribs->stride, (EGLint *)attribs->offset)) {
		return false;
	}

	attribs->width = width;
	attribs->height = height;
	attribs->flags = flags;
	return true;
}

bool wlr_egl_destroy_surface(struct wlr_egl *egl, EGLSurface surface) {
	if (!surface) {
		return true;
	}
	if (eglGetCurrentContext() == egl->context &&
			eglGetCurrentSurface(EGL_DRAW) == surface) {
		// Reset the current EGL surface in case it's the one we're destroying,
		// otherwise the next wlr_egl_make_current call will result in a
		// use-after-free.
		wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);
	}
	return eglDestroySurface(egl->display, surface);
}
