/*
 * Copyright (c) 2026 The Lindroid Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <android-config.h>
#include <drm/drm_fourcc.h>
#include <ws.h>
#include <malloc.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <mutex>
#include <algorithm>
#include <gbm.h>
#include <wayland-client.h>
#include <xf86drm.h>
#include <sys/ioctl.h>
#include <windowbuffer.h>
extern "C" {
#include <eglplatformcommon.h>
};

#include <eglhybris.h>

#include <EGL/eglext.h>

extern "C" {
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-egl-backend.h>
}

#include <hybris/gralloc/gralloc.h>

#include "logging.h"
#include "evdi_drm.h"
#include <hybris/gralloc/gralloc.h>
#include "wayland_window.h"
#include "gbm_native_window.h"

#include <stdint.h>

struct LindroidDisplay {
    _EGLDisplay base;
    enum { AUTO, WAYLAND, GBM } mode;

    EGLNativeDisplayType native_display;
    wl_display *wl_dpy;
    bool owns_wl_dpy;
};

static const char *  (*_eglQueryString)(EGLDisplay dpy, EGLint name) = NULL;
static __eglMustCastToProperFunctionPointerType (*_eglGetProcAddress)(const char *procname) = NULL;

static bool lindroid_native_display_is_gbm(EGLNativeDisplayType display)
{
    const struct gbm_device *gbm;

    if (!display)
        return false;

    gbm = reinterpret_cast<const struct gbm_device *>(display);

    if (gbm->v0.backend_version != GBM_BACKEND_ABI_VERSION)
        return false;
    if (gbm->v0.fd < 0)
        return false;
    if (!gbm->v0.bo_create || !gbm->v0.surface_create)
        return false;

    return true;
}

static std::mutex _nativewindows_mutex;
int drm_fd = -1;
struct gbm_device *gbm_dev = nullptr;

static int drm_auth_magic(int fd, drm_magic_t magic) {
    drm_auth_t auth;
    auth.magic = magic;
    if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth)) {
        return -errno;
    }
    return 0;
}

static bool drm_is_master(int fd) {
    return drm_auth_magic(fd, 0) != -EACCES;
}

int evdi_open(char *device_path) {
	int fd = open(device_path, O_RDWR);

	if (fd < 0) {
		fprintf(stderr, "Failed to open device");
		return -1;
	}

	if (drm_is_master(fd)) {
		fprintf(stderr, "Process has master on %s", device_path);
		if (ioctl(fd, DRM_IOCTL_DROP_MASTER, NULL) < 0) {
			fprintf(stderr, "Drop master on %s", device_path);
			close(fd);
			return -1;
		}
	}

	if (drm_is_master(fd)) {
		fprintf(stderr, "Drop master on %s");
		close(fd);
		return -1;
	}

	return fd;
}

int evdi_get_native_handle_t(int native_handle_id, native_handle_t **handle, bool import) {
	struct drm_evdi_gbm_get_buff cmd;
	int ret = 0;
	native_handle_t *tmp_handle;
	cmd.id = native_handle_id;
	cmd.native_handle = malloc(max_native_handle_size);

	ret = ioctl(drm_fd, DRM_IOCTL_EVDI_GBM_GET_BUFF, &cmd);
	if (ret < 0) {
		fprintf(stderr, "DRM_IOCTL_EVDI_GBM_GET_BUFF failed, do fd come from non lindroid driver?");
		return ret;
	}

	tmp_handle = (native_handle_t*)cmd.native_handle;
	if (!tmp_handle) {
		fprintf(stderr, "failed to clone handle");
		return -ENOMEM;
	}

	if(import && hybris_gralloc_import_buffer((const native_handle_t*)tmp_handle, (buffer_handle_t*)handle)) {
		fprintf(stderr, "failed to import buf, attempting to use as is");
		*handle = tmp_handle;
	} else {
		native_handle_close(tmp_handle);
		native_handle_delete(tmp_handle);
	}
	return ret;
}

uint32_t get_gbm_pixel_format(int hal_format)
{
    uint32_t format;

    switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
        format = GBM_FORMAT_ABGR8888;
        break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
        format = GBM_FORMAT_XRGB8888;
        break;
    case HAL_PIXEL_FORMAT_RGB_888:
        format = GBM_FORMAT_RGB888;
        break;
    case HAL_PIXEL_FORMAT_RGB_565:
        format = GBM_FORMAT_RGB565;
        break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        format = GBM_FORMAT_ABGR8888;
        break;
    case HAL_PIXEL_FORMAT_YV12:
        format = GBM_FORMAT_GR88;
        break;
    case HAL_PIXEL_FORMAT_RGBA_FP16:
        format = GBM_FORMAT_ABGR16161616F;
        break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:
        format = GBM_FORMAT_ABGR2101010;
        break;
    default:
        format = GBM_FORMAT_ABGR8888;
        break;
    }

    return format;
}

extern "C" EGLBoolean egl_get_win_buf(EGLint width, EGLint height, EGLint usage, EGLint format, EGLint stride,
                                                                     native_handle_t *native, EGLClientBuffer *buffer)
{
	if(!native) {
		fprintf(stderr, "egl_get_win_buf: native handle cant be NULL!");
		return EGL_FALSE;
	}
	RemoteWindowBuffer *buf = new RemoteWindowBuffer(width, height, stride, format, usage, (buffer_handle_t)native);
	buf->common.incRef(&buf->common);
	*buffer = (EGLClientBuffer) static_cast<ANativeWindowBuffer *>(buf);
	return EGL_TRUE;
}

static int lindroid_init_evdi()
{
	if (drm_fd >= 0)
		return 0;

	drm_fd = evdi_open("/dev/dri/by-path/platform-evdi-lindroid.0-card");
	if (drm_fd < 0)
		return -1;

	gbm_dev = gbm_create_device(drm_fd);
	return gbm_dev ? 0 : -1;
}

static bool lindroid_native_window_is_gbm(EGLNativeWindowType win)
{
    struct gbm_hybris_surface *surf = (struct gbm_hybris_surface *)win;
    struct gbm_device *gbm;

    if (!surf)
        return false;

    gbm = surf->base.gbm;
    if (!gbm)
        return false;

    if (gbm->v0.backend_version != GBM_BACKEND_ABI_VERSION)
        return false;

    if (gbm->v0.fd < 0)
        return false;

    return gbm->v0.bo_create && gbm->v0.surface_create &&
           surf->base.v0.width > 0 && surf->base.v0.height > 0;
}

extern "C" void lindroid_drmws_init_module(struct ws_egl_interface *egl_iface)
{
	hybris_gralloc_initialize(0);
	eglplatformcommon_init(egl_iface);
}

extern "C" _EGLDisplay *lindroid_drmws_GetDisplay(EGLNativeDisplayType display)
{
    LindroidDisplay *ldpy = new LindroidDisplay();
    ldpy->native_display = display;
    ldpy->wl_dpy = nullptr;
    ldpy->owns_wl_dpy = false;

    if (display && lindroid_native_display_is_gbm(display)) {
        ldpy->mode = LindroidDisplay::GBM;
    } else if (display) {
        ldpy->mode = LindroidDisplay::WAYLAND;
        ldpy->wl_dpy = (wl_display *)display;
    } else {
        ldpy->mode = LindroidDisplay::AUTO;
    }

    return &ldpy->base;
}

extern "C" void lindroid_drmws_releaseDisplay(_EGLDisplay *dpy)
{
    LindroidDisplay *ldpy = (LindroidDisplay *)dpy;

    if (ldpy->owns_wl_dpy && ldpy->wl_dpy)
        wl_display_disconnect(ldpy->wl_dpy);

    delete ldpy;
}

extern "C" void lindroid_drmws_eglInitialized(_EGLDisplay *dpy)
{
}

extern "C" void lindroid_drmws_Terminate(_EGLDisplay *dpy)
{
}

extern "C" EGLNativeWindowType lindroid_drmws_CreateWindow(EGLNativeWindowType win, _EGLDisplay *display)
{
    LindroidDisplay *ldpy = (LindroidDisplay *)display;
    bool use_gbm = false;

    if (!win) {
        HYBRIS_ERROR("Invalid native window");
        abort();
    }

    if (ldpy->mode == LindroidDisplay::GBM)
        use_gbm = true;
    else if (ldpy->mode == LindroidDisplay::AUTO &&
             ldpy->native_display &&
             lindroid_native_display_is_gbm(ldpy->native_display))
        use_gbm = true;

    if (use_gbm) {
        GbmNativeWindow *window = new GbmNativeWindow((gbm_surface *)win);
        window->common.incRef(&window->common);
        return (EGLNativeWindowType) static_cast<struct ANativeWindow *>(window);
    }

    struct wl_egl_window *wl_window = (struct wl_egl_window *)win;

    if (!wl_window) {
        HYBRIS_ERROR("Running with EGL_PLATFORM=wayland without setup wayland environment is not possible");
        HYBRIS_ERROR("If you want to run a standlone EGL client do it like this:");
        HYBRIS_ERROR(" $ export EGL_PLATFORM=null");
        HYBRIS_ERROR(" $ test_glevs2");
        abort();
    }

    if (!ldpy->wl_dpy) {
        ldpy->wl_dpy = wl_display_connect(NULL);
        if (!ldpy->wl_dpy) {
            fprintf(stderr, "Fatal: failed to connect to the server!");
            abort();
        }
        ldpy->owns_wl_dpy = true;
    }

    WaylandNativeWindow *window = new WaylandNativeWindow(wl_window, ldpy->wl_dpy, NULL);
    window->common.incRef(&window->common);
    return (EGLNativeWindowType) static_cast<struct ANativeWindow *>(window);
}

extern "C" void lindroid_drmws_DestroyWindow(EGLNativeWindowType win)
{
	((struct ANativeWindow *)win)->common.decRef(&((struct ANativeWindow *)win)->common);
}

extern "C" __eglMustCastToProperFunctionPointerType lindroid_drmws_eglGetProcAddress(const char *procname)
{
	return eglplatformcommon_eglGetProcAddress(procname);
}

extern "C" void lindroid_drmws_passthroughImageKHR(EGLContext *ctx, EGLenum *target, EGLClientBuffer *buffer, const EGLint **attrib_list)
{
	int width = 0, height = 0, format = 0, stride = 0;
	native_handle_t* full_handle = nullptr;

	int plane_fds[4];
	int num_planes = 0;
	int meta_fd = -1;

	// Parse Image parameters
	for (const EGLint *attr = *attrib_list; attr && *attr != EGL_NONE; attr += 2) {
		switch (attr[0]) {
			case EGL_DMA_BUF_PLANE0_FD_EXT:
				plane_fds[num_planes++] = attr[1];
				break;
			case EGL_DMA_BUF_PLANE1_FD_EXT:
				plane_fds[num_planes++] = attr[1];
				break;
			case EGL_DMA_BUF_PLANE2_FD_EXT:
				plane_fds[num_planes++] = attr[1];
				break;
			case EGL_DMA_BUF_PLANE3_FD_EXT:
				plane_fds[num_planes++] = attr[1];
				break;
			case EGL_WIDTH:
				width = attr[1];
				break;
			case EGL_HEIGHT:
				height = attr[1];
				break;
			case EGL_LINUX_DRM_FOURCC_EXT:
				format = attr[1];
				break;
			case EGL_DMA_BUF_PLANE0_PITCH_EXT:
				stride = attr[1] / 4; // Our libgbm *4's the stride to match drm expectations
				break;
			default:
				break;
		}
	}

    if (num_planes == 0) {
        fprintf(stderr, "No DMA-BUF planes\n");
        abort();
    }

	// As per https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import.txt those valuies are mandatory
	if (width == 0) {
		fprintf(stderr, "Fatal: EGL_WIDTH is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if (height == 0) {
		fprintf(stderr, "Fatal: EGL_HEIGHT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if (format == 0) {
		fprintf(stderr, "Fatal: EGL_LINUX_DRM_FOURCC_EXT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if (stride == 0) {
		fprintf(stderr, "Fatal: EGL_DMA_BUF_PLANE0_PITCH_EXT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if (lindroid_init_evdi() < 0) {
		fprintf(stderr, "Fatal: failed to open evdi device\n");
		abort();
	}

    meta_fd = plane_fds[num_planes - 1];
    num_planes--;

    int header[4];
    if (pread(meta_fd, header, sizeof(header), 0) != (ssize_t)sizeof(header)) {
        fprintf(stderr, "Failed to read meta_fd\n");
        abort();
    }

    int evdi_buff_id = header[0];
    int version = header[1];
    int numFds = header[2];
    int numInts = header[3];

    if (evdi_buff_id == -1 && numInts > 0) {
	    int meta_extra[numInts];
	    if (pread(meta_fd, meta_extra, numInts * sizeof(int), sizeof(header)) != (ssize_t)(numInts * sizeof(int))) {
	        fprintf(stderr, "Failed to read extra metadata\n");
	        abort();
	    }

	    native_handle_t* nh = native_handle_create(num_planes, numInts);
        nh->version = version;

        for (int i = 0; i < num_planes; i++)
            nh->data[i] = plane_fds[i];

        for (int i = 0; i < numInts; i++)
            nh->data[num_planes + i] = meta_extra[i];

        const native_handle_t* out_handle = nullptr;
        if (hybris_gralloc_import_buffer(nh, &out_handle) == 0 && out_handle)
            full_handle = const_cast<native_handle_t*>(out_handle);

        native_handle_delete(nh);
	} else {
		// Attempt to get buffer from create-disp
		if (evdi_get_native_handle_t(evdi_buff_id, &full_handle, true) != 0 || !full_handle) {
			fprintf(stderr, "Fatal: failed to get native handle\n");
			abort();
		}
    }

	// Prevent HWC from alpha-blending garbage
	int hal_format = (format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_XBGR8888)
					 ? HAL_PIXEL_FORMAT_RGBX_8888
					 : HAL_PIXEL_FORMAT_RGBA_8888;

	// Convert native handle to EGLClientBuffer
	if (!egl_get_win_buf(width, height,
						 GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER,
						 hal_format, stride,
						 full_handle, buffer)) {
		fprintf(stderr, "egl_get_win_buf failed\n");
		abort();
	}

	*attrib_list = NULL;
	*ctx = EGL_NO_CONTEXT;
	*target = EGL_NATIVE_BUFFER_ANDROID;
}

extern "C" void lindroid_drmws_destroyImageKHR(EGLImageKHR image) {
	struct egl_image *img = (egl_image*)image;
	if(img->ws_buffer) {
		native_handle_close(((ANativeWindowBuffer*)img->ws_buffer)->handle);
	}
}

extern "C" const char *lindroid_drmws_eglQueryString(EGLDisplay dpy, EGLint name, const char *(*real_eglQueryString)(EGLDisplay dpy, EGLint name))
{
	const char *ret = eglplatformcommon_eglQueryString(dpy, name, real_eglQueryString);
	if (ret && name == EGL_EXTENSIONS)
	{
		static char eglextensionsbuf[2048];
		snprintf(eglextensionsbuf, 2046, "%s %s", ret,
			"EGL_EXT_swap_buffers_with_damage EGL_WL_create_wayland_buffer_from_image EGL_EXT_platform_base EGL_KHR_platform_gbm EGL_EXT_image_dma_buf_import EGL_EXT_image_dma_buf_import_modifiers"
		);
		ret = eglextensionsbuf;
	}
	return ret;
}

extern "C" void lindroid_drmws_prepareSwap(EGLDisplay dpy, EGLNativeWindowType win, EGLint *damage_rects, EGLint damage_n_rects)
{
    (void)dpy;
    if (!win)
        return;

    static_cast<EGLBaseNativeWindow *>((struct ANativeWindow *)win)->prepareSwap(damage_rects, damage_n_rects);
}

extern "C" void lindroid_drmws_finishSwap(EGLDisplay dpy, EGLNativeWindowType win)
{
    (void)dpy;
    if (!win)
        return;

    static_cast<EGLBaseNativeWindow *>((struct ANativeWindow *)win)->finishSwap();
}

extern "C" void lindroid_drmws_setSwapInterval(EGLDisplay dpy, EGLNativeWindowType win, EGLint interval)
{
    (void)dpy;
    if (!win)
        return;

    static_cast<EGLBaseNativeWindow *>((struct ANativeWindow *)win)->setSwapInterval(interval);
}

extern "C" void lindroid_drmwws_getConfigAttrib(EGLDisplay *dpy, EGLConfig *config, EGLint *attribute, EGLint *value)
{
    if (attribute && value && *attribute == EGL_NATIVE_VISUAL_ID) {
        EGLint tmp = (EGLint)get_gbm_pixel_format((uint32_t)*value);
        *value = tmp;
    }
}

// DRM FourCC matches 1-1 with GBM one
EGLint lindroid_formats[7] = {GBM_FORMAT_ABGR8888, GBM_FORMAT_XRGB8888, GBM_FORMAT_RGB888, GBM_FORMAT_RGB565, GBM_FORMAT_GR88, GBM_FORMAT_ABGR16161616F, GBM_FORMAT_ABGR2101010};

extern "C" EGLBoolean lindroid_drmws_queryDmaBufFormatsEXT(EGLDisplay dpy, EGLint max_formats, EGLint *formats, EGLint *num_formats)
{
	if(max_formats < 0)
		return EGL_FALSE;

	*num_formats = sizeof(lindroid_formats) / sizeof(lindroid_formats[0]);
	if(max_formats == 0)
		return EGL_TRUE;

	for(int i = 0; i < std::min(sizeof(lindroid_formats) / sizeof(lindroid_formats[0]), static_cast<size_t>(max_formats)); i++) {
		formats[i] = lindroid_formats[i];
	}
	return EGL_TRUE;
}

EGLuint64KHR lindroid_modifiers_common[1] = {DRM_FORMAT_MOD_LINEAR};

extern "C" EGLBoolean lindroid_drmws_queryDmaBufModifiersEXT(EGLDisplay dpy, EGLint format, EGLint max_modifiers, EGLuint64KHR *modifiers, EGLBoolean *external_only, EGLint *num_modifiers)
{
	if(max_modifiers < 0)
		return EGL_FALSE;

	*num_modifiers = sizeof(lindroid_modifiers_common) / sizeof(lindroid_modifiers_common[0]);
	if(max_modifiers == 0)
		return EGL_TRUE;

	*num_modifiers = sizeof(lindroid_modifiers_common) / sizeof(lindroid_modifiers_common[0]);

	for(int i = 0; i < std::min(sizeof(lindroid_modifiers_common) / sizeof(lindroid_modifiers_common[0]), static_cast<size_t>(max_modifiers)); i++) {
		modifiers[i] = lindroid_modifiers_common[i];
		if(external_only)
			external_only[i] = EGL_FALSE;
	}

	return EGL_TRUE;
}

struct ws_module ws_module_info = {
	lindroid_drmws_init_module,
	lindroid_drmws_GetDisplay,
	lindroid_drmws_Terminate,
	lindroid_drmws_CreateWindow,
	lindroid_drmws_DestroyWindow,
	lindroid_drmws_eglGetProcAddress,
	lindroid_drmws_passthroughImageKHR,
	lindroid_drmws_eglQueryString,
	lindroid_drmws_prepareSwap,
	lindroid_drmws_finishSwap,
	lindroid_drmws_setSwapInterval,
	lindroid_drmws_releaseDisplay,
	NULL,
	lindroid_drmws_destroyImageKHR,
	lindroid_drmwws_getConfigAttrib,
	lindroid_drmws_queryDmaBufFormatsEXT,
	lindroid_drmws_queryDmaBufModifiersEXT,
};

// vim:ts=4:sw=4:noexpandtab
