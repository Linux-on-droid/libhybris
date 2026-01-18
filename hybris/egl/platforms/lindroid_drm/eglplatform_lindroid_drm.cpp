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

struct WaylandDisplay {
        _EGLDisplay base;

        // this is protected via mutex in egl.c
        int init_count;
        wl_display *wl_dpy;
        wl_event_queue *queue;
        wl_display *wl_dpy_wrapper;
};

static const char *  (*_eglQueryString)(EGLDisplay dpy, EGLint name) = NULL;
static __eglMustCastToProperFunctionPointerType (*_eglGetProcAddress)(const char *procname) = NULL;
static EGLSyncKHR (*_eglCreateSyncKHR)(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list) = NULL;
static EGLBoolean (*_eglDestroySyncKHR)(EGLDisplay dpy, EGLSyncKHR sync) = NULL;
static EGLint (*_eglClientWaitSyncKHR)(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout) = NULL;

//static std::vector<HWComposerNativeWindow *> _nativewindows;
static std::mutex _nativewindows_mutex;
int drm_fd;
struct gbm_device *gbm_dev;

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

extern "C" void lindroid_drmws_init_module(struct ws_egl_interface *egl_iface)
{
	// TBD: Is that the best way?
	drm_fd = evdi_open("/dev/dri/by-path/platform-evdi-lindroid.0-card");
	gbm_dev = gbm_create_device(drm_fd);
	hybris_gralloc_initialize(0);
	eglplatformcommon_init(egl_iface);
}

static void _init_egl_funcs(EGLDisplay display)
{
	if (_eglQueryString != NULL)
		return;

	_eglQueryString = (const char * (*)(void*, int))
			hybris_android_egl_dlsym("eglQueryString");
	assert(_eglQueryString);
	_eglGetProcAddress = (__eglMustCastToProperFunctionPointerType (*)(const char *))
			hybris_android_egl_dlsym("eglGetProcAddress");
	assert(_eglGetProcAddress);

	const char *extensions = (*_eglQueryString)(display, EGL_EXTENSIONS);

	if (strstr(extensions, "EGL_KHR_fence_sync")) {
		_eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)
				(*_eglGetProcAddress)("eglCreateSyncKHR");
		assert(_eglCreateSyncKHR);
		_eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)
				(*_eglGetProcAddress)("eglDestroySyncKHR");
		assert(_eglDestroySyncKHR);
		_eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)
				(*_eglGetProcAddress)("eglClientWaitSyncKHR");
		assert(_eglClientWaitSyncKHR);
	}
}

extern "C" _EGLDisplay *lindroid_drmws_GetDisplay(EGLNativeDisplayType display)
{
	WaylandDisplay *wdpy = new WaylandDisplay;
	wdpy->wl_dpy = (wl_display *) display;
	if (!wdpy->wl_dpy) {
		wdpy->wl_dpy = wl_display_connect(NULL);
		if (!wdpy->wl_dpy) {
			fprintf(stderr, "Fatal: failed to connect to the server!");
			abort();
		}
	}

	return &wdpy->base;
}

extern "C" void lindroid_drmws_releaseDisplay(_EGLDisplay *dpy)
{
	WaylandDisplay *wdpy = (WaylandDisplay *)dpy;
	delete wdpy;
}

extern "C" void lindroid_drmws_eglInitialized(_EGLDisplay *dpy)
{
	WaylandDisplay *wdpy = (WaylandDisplay *)dpy;
}

extern "C" void lindroid_drmws_Terminate(_EGLDisplay *dpy)
{
	// Do we even have anything to clean up?
}

extern "C" EGLNativeWindowType lindroid_drmws_CreateWindow(EGLNativeWindowType win, _EGLDisplay *display)
{
	struct wl_egl_window *wl_window = (struct wl_egl_window*) win;
	struct wl_display *wl_display = (struct wl_display*) display;

	if (wl_window == 0 || wl_display == 0) {
		HYBRIS_ERROR("Running with EGL_PLATFORM=wayland without setup wayland environment is not possible");
		HYBRIS_ERROR("If you want to run a standlone EGL client do it like this:");
		HYBRIS_ERROR(" $ export EGL_PLATFORM=null");
		HYBRIS_ERROR(" $ test_glevs2");
		abort();
	}

	WaylandDisplay *wdpy = (WaylandDisplay *)display;

	WaylandNativeWindow *window = new WaylandNativeWindow((struct wl_egl_window *) win, wdpy->wl_dpy, NULL);
	window->common.incRef(&window->common);
	return (EGLNativeWindowType) static_cast<struct ANativeWindow *>(window);
}

extern "C" void lindroid_drmws_DestroyWindow(EGLNativeWindowType win)
{
	WaylandNativeWindow *window = static_cast<WaylandNativeWindow *>((struct ANativeWindow *)win);
	window->common.decRef(&window->common);
}

extern "C" __eglMustCastToProperFunctionPointerType lindroid_drmws_eglGetProcAddress(const char *procname)
{
	return eglplatformcommon_eglGetProcAddress(procname);
}

extern "C" void lindroid_drmws_passthroughImageKHR(EGLContext *ctx, EGLenum *target, EGLClientBuffer *buffer, const EGLint **attrib_list)
{
	int buff_fd, native_handle_id;
	int width = 0, height = 0, format = 0, stride = 0;
	native_handle_t* full_handle;

	// Parse Image parameters
	for (const EGLint *attr = *attrib_list; attr && *attr != EGL_NONE; attr += 2) {
		switch (attr[0]) {
			case EGL_DMA_BUF_PLANE0_FD_EXT:
				buff_fd = (int)(attr[1]);
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
				stride = attr[1];
				break;
			default:
				break;
		}
	}

	// As per https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import.txt those valuies are mandatory
	if(!buff_fd) {
		fprintf(stderr, "Fatal: EGL_DMA_BUF_PLANE0_FD_EXT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if(width == 0) {
		fprintf(stderr, "Fatal: EGL_WIDTH is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if(height == 0) {
		fprintf(stderr, "Fatal: EGL_HEIGHT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if(format == 0) {
		fprintf(stderr, "Fatal: EGL_LINUX_DRM_FOURCC_EXT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if(stride == 0) {
		fprintf(stderr, "Fatal: EGL_DMA_BUF_PLANE0_PITCH_EXT is missing from EGL_LINUX_DMA_BUF_EXT");
		abort();
	}

	if (fcntl(buff_fd, F_GETFD) == -1) {
		fprintf(stderr, "Fatal: Invalid or closed file descriptor: %d", buff_fd);
		abort();
	}

	if (lseek(buff_fd, 0, SEEK_SET) == -1) {
		fprintf(stderr, "Fatal: Failed to seek fd: %d, do fd come from non lindroid driver?", buff_fd);
		abort();
	}

	if (read(buff_fd, &native_handle_id, sizeof(int)) != sizeof(int)) {
		fprintf(stderr, "Fatal: failed to read fd: %d", buff_fd);
		abort();
	}

	// Attempt to get buffer from create-disp
	if(evdi_get_native_handle_t(native_handle_id, &full_handle, true)) {
		fprintf(stderr, "Fatal: failed to get native handle");
		abort();
	}

	// Convert native handle to EGLClientBuffer
	egl_get_win_buf(width, height, GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER, HAL_PIXEL_FORMAT_RGBA_8888, stride, (native_handle_t *)full_handle, buffer);

	static EGLint native_buffer_attribs[7];
        native_buffer_attribs[0] = EGL_WIDTH;
        native_buffer_attribs[1] = width;
        native_buffer_attribs[2] = EGL_HEIGHT;
        native_buffer_attribs[3] = height;
        native_buffer_attribs[4] = EGL_NONE;
        *attrib_list = native_buffer_attribs;

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
			"EGL_EXT_swap_buffers_with_damage EGL_WL_create_wayland_buffer_from_image EGL_EXT_platform_base EGL_KHR_platform_gbm"
		);
		ret = eglextensionsbuf;
	}
	return ret;
}

extern "C" void lindroid_drmws_prepareSwap(EGLDisplay dpy, EGLNativeWindowType win, EGLint *damage_rects, EGLint damage_n_rects)
{
        WaylandNativeWindow *window = static_cast<WaylandNativeWindow *>((struct ANativeWindow *)win);
        window->prepareSwap(damage_rects, damage_n_rects);
}

extern "C" void lindroid_drmws_finishSwap(EGLDisplay dpy, EGLNativeWindowType win)
{
        _init_egl_funcs(dpy);
        WaylandNativeWindow *window = static_cast<WaylandNativeWindow *>((struct ANativeWindow *)win);
        if (_eglCreateSyncKHR) {
                EGLSyncKHR sync = (*_eglCreateSyncKHR)(dpy, EGL_SYNC_FENCE_KHR, NULL);
                (*_eglClientWaitSyncKHR)(dpy, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
                (*_eglDestroySyncKHR)(dpy, sync);
        }
        window->finishSwap();
}

extern "C" void lindroid_drmws_setSwapInterval(EGLDisplay dpy, EGLNativeWindowType win, EGLint interval)
{
        WaylandNativeWindow *window = static_cast<WaylandNativeWindow *>((struct ANativeWindow *)win);
        window->setSwapInterval(interval);
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
};

// vim:ts=4:sw=4:noexpandtab
