/****************************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Carsten Munk <carsten.munk@jollamobile.com>
** All rights reserved.
**
** This file is part of X11 enablement for libhybris
**
** You may use this file under the terms of the GNU Lesser General
** Public License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
** Lesser General Public License for more details.
**
****************************************************************************************/

#include <android-config.h>
#include <ws.h>
#include <malloc.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
extern "C" {
#include <eglplatformcommon.h>
};
#include <eglhybris.h>

#include <EGL/eglext.h>

extern "C" {
#include <wayland-client.h>
#include <wayland-egl.h>
}

#include <hardware/gralloc.h>
#include <hybris/gralloc/gralloc.h>
#include "x11_window.h"
#include "logging.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "xcb_drihybris.h"

static Display *x11_display = NULL;
static xcb_connection_t *xcb_connection = NULL;
static bool have_drihybris = false;

static const char *  (*_eglQueryString)(EGLDisplay dpy, EGLint name) = NULL;
static __eglMustCastToProperFunctionPointerType (*_eglGetProcAddress)(const char *procname) = NULL;
static EGLSyncKHR (*_eglCreateSyncKHR)(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list) = NULL;
static EGLBoolean (*_eglDestroySyncKHR)(EGLDisplay dpy, EGLSyncKHR sync) = NULL;
static EGLint (*_eglClientWaitSyncKHR)(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout) = NULL;

struct X11Display {
	_EGLDisplay base;
	Display *xl_display;
};

extern "C" void x11ws_init_module(struct ws_egl_interface *egl_iface)
{
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

extern "C" _EGLDisplay *x11ws_GetDisplay(EGLNativeDisplayType display)
{
	X11Display *xdpy = new X11Display;
	xdpy->xl_display = (Display *)display;

	if (!x11_display && xdpy->xl_display) {
		x11_display = xdpy->xl_display;

		// Check if we have drihybris support
		xcb_connection = XGetXCBConnection(x11_display);
		const xcb_query_extension_reply_t *extension;

		xcb_prefetch_extension_data (xcb_connection, &xcb_drihybris_id);
		extension = xcb_get_extension_data(xcb_connection, &xcb_drihybris_id);
		if (extension && extension->present)
			have_drihybris = true;
	}

	return &xdpy->base;
}

extern "C" void x11ws_Terminate(_EGLDisplay *dpy)
{
	X11Display *xdpy = (X11Display *)dpy;
	int ret = 0;
	delete xdpy;
}

extern "C" EGLNativeWindowType x11ws_CreateWindow(EGLNativeWindowType win, _EGLDisplay *display)
{
	Window xlib_window = (Window) win;
	X11Display *xdpy = (X11Display *)display;

	if (win == 0 || xdpy->xl_display == 0) {
		HYBRIS_ERROR("Running with EGL_PLATFORM=x11 without X server is not possible");
		HYBRIS_ERROR("If you want to run a standlone EGL client do it like this:");
		HYBRIS_ERROR(" $ export EGL_PLATFORM=null");
		HYBRIS_ERROR(" $ test_glevs2");
		abort();
	}

	X11NativeWindow *window = new X11NativeWindow(xdpy->xl_display, xlib_window,
                                                  have_drihybris);
	window->common.incRef(&window->common);
	return (EGLNativeWindowType) static_cast<struct ANativeWindow *>(window);
}

extern "C" void x11ws_DestroyWindow(EGLNativeWindowType win)
{
	X11NativeWindow *window = static_cast<X11NativeWindow *>((struct ANativeWindow *)win);
	window->common.decRef(&window->common);
}

extern "C" __eglMustCastToProperFunctionPointerType x11ws_eglGetProcAddress(const char *procname)
{
	return eglplatformcommon_eglGetProcAddress(procname);
}

extern "C" EGLBoolean eglplatformcommon_eglHybrisCreateRemoteBuffer(EGLint width, EGLint height, EGLint usage, EGLint format, EGLint stride,
																	int num_ints, int *ints, int num_fds, int *fds, EGLClientBuffer *buffer);

extern "C" void x11ws_passthroughImageKHR(EGLContext *ctx, EGLenum *target, EGLClientBuffer *buffer, const EGLint **attrib_list)
{
	if (*target == EGL_NATIVE_PIXMAP_KHR && have_drihybris)
	{
		TRACE("target: EGL_NATIVE_PIXMAP_KHR");
		xcb_drihybris_buffer_from_pixmap_cookie_t bp_cookie;
		xcb_drihybris_buffer_from_pixmap_reply_t  *bp_reply;
		xcb_drawable_t drawable = (xcb_drawable_t) (uintptr_t) *buffer;
		xcb_generic_error_t *error;

		bp_cookie = xcb_drihybris_buffer_from_pixmap(xcb_connection,
													drawable);

		bp_reply = xcb_drihybris_buffer_from_pixmap_reply(xcb_connection,
														bp_cookie, &error);

		if (bp_reply) {
			EGLClientBuffer buf;

			eglplatformcommon_eglHybrisCreateRemoteBuffer(
				bp_reply->width, bp_reply->height, GRALLOC_USAGE_HW_TEXTURE,
				HAL_PIXEL_FORMAT_RGBA_8888, bp_reply->stride,
				bp_reply->num_ints,
				(int*)xcb_drihybris_buffer_from_pixmap_ints(bp_reply),
				bp_reply->num_fds, xcb_drihybris_buffer_from_pixmap_fds(bp_reply),
				&buf);

			TRACE("created remote EGL buffer, set target to EGL_NATIVE_BUFFER_ANDROID");

			*buffer = (EGLClientBuffer) (ANativeWindowBuffer *) buf;
			*target = EGL_NATIVE_BUFFER_ANDROID;
			*ctx = EGL_NO_CONTEXT;
			*attrib_list = NULL;
		} else {
			HYBRIS_ERROR("xcb_drihybris_buffer_from_pixmap call failed");
			if (error)
				HYBRIS_ERROR("xcb_drihybris_buffer_from_pixmap error code: %d\n",
							error->error_code);
		}
	}
	eglplatformcommon_passthroughImageKHR(ctx, target, buffer, attrib_list);
}

extern "C" const char *x11ws_eglQueryString(EGLDisplay dpy, EGLint name, const char *(*real_eglQueryString)(EGLDisplay dpy, EGLint name))
{
	const char *ret = eglplatformcommon_eglQueryString(dpy, name, real_eglQueryString);
	if (ret && name == EGL_EXTENSIONS)
	{
		static char eglextensionsbuf[1024];
		snprintf(eglextensionsbuf, 1022, "%s %s", ret,
			"EGL_EXT_swap_buffers_with_damage EGL_KHR_image_pixmap"
		);
		ret = eglextensionsbuf;
	}
	return ret;
}

extern "C" void x11ws_prepareSwap(EGLDisplay dpy, EGLNativeWindowType win, EGLint *damage_rects, EGLint damage_n_rects)
{
	X11NativeWindow *window = static_cast<X11NativeWindow *>((struct ANativeWindow *)win);
	window->prepareSwap(damage_rects, damage_n_rects);
}

extern "C" void x11ws_finishSwap(EGLDisplay dpy, EGLNativeWindowType win)
{
	_init_egl_funcs(dpy);
	X11NativeWindow *window = static_cast<X11NativeWindow *>((struct ANativeWindow *)win);
	if (_eglCreateSyncKHR) {
		EGLSyncKHR sync = (*_eglCreateSyncKHR)(dpy, EGL_SYNC_FENCE_KHR, NULL);
		(*_eglClientWaitSyncKHR)(dpy, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
		(*_eglDestroySyncKHR)(dpy, sync);
	}
	window->finishSwap();
}

extern "C" void x11ws_setSwapInterval(EGLDisplay dpy, EGLNativeWindowType win, EGLint interval)
{
	X11NativeWindow *window = static_cast<X11NativeWindow *>((struct ANativeWindow *)win);
	window->setSwapInterval(interval);
}

extern "C" EGLBoolean x11ws_eglGetConfigAttrib(struct _EGLDisplay *display, EGLConfig config, EGLint attribute, EGLint *value)
{
	TRACE("attribute:%i", attribute);
	if (attribute == EGL_NATIVE_VISUAL_ID)
	{
		X11Display *xdpy = (X11Display *)display;
		XVisualInfo visinfo_template;
		XVisualInfo *visinfo = NULL;
		int visinfos_count = 0;

		visinfo_template.depth = 32;
		visinfo = XGetVisualInfo (xdpy->xl_display,
							VisualDepthMask,
							&visinfo_template,
							&visinfos_count);

		if (visinfos_count)
		{
			TRACE("visinfo.visualid:%i", attribute);
			*value = visinfo->visualid;
			return EGL_TRUE;
		}

	}
	return EGL_FALSE;
}

struct ws_module ws_module_info = {
	x11ws_init_module,
	x11ws_GetDisplay,
	x11ws_Terminate,
	x11ws_CreateWindow,
	x11ws_DestroyWindow,
	x11ws_eglGetProcAddress,
	x11ws_passthroughImageKHR,
	x11ws_eglQueryString,
	x11ws_prepareSwap,
	x11ws_finishSwap,
	x11ws_setSwapInterval,
	x11ws_eglGetConfigAttrib
};
