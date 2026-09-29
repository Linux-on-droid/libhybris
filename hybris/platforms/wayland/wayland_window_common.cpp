/****************************************************************************************
 **
 ** Copyright (C) 2013-2022 Jolla Ltd.
 ** Copyright (C) 2024 Jollyboys Ltd.
 ** All rights reserved.
 **
 ** This file is part of Wayland enablement for libhybris
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
#include <hardware/gralloc.h>
#include "wayland_window.h"
#include <wayland-egl-backend.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "logging.h"

#ifdef WANT_LINDROID_DRM
#include <gbm.h>
#include <fcntl.h>
#include <wayland-client.h>
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include <sys/mman.h>
#include <vector>
#ifdef HYBRIS_VULKAN_WS
#include <sys/ioctl.h>
#include <xf86drm.h>
#include "evdi_drm.h"
#endif
#endif // WANT_LINDROID_DRM

#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=2 || ANDROID_VERSION_MAJOR>=5
extern "C" {
#include <sync/sync.h>
}
#endif

static void
buffer_create_sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
   struct wl_callback **created_callback = static_cast<struct wl_callback **>(data);

   *created_callback = NULL;
   wl_callback_destroy(callback);
}

static const struct wl_callback_listener buffer_create_sync_listener = {
   buffer_create_sync_callback
};

void WaylandNativeWindowBuffer::wlbuffer_from_native_handle(struct android_wlegl *android_wlegl,
                                                            struct wl_display *display,
                                                            struct wl_event_queue *queue)
{
    struct wl_array ints;
    int *ints_data;
    struct android_wlegl_handle *wlegl_handle;

    wl_array_init(&ints);
    ints_data = (int*) wl_array_add(&ints, handle->numInts*sizeof(int));
    memcpy(ints_data, handle->data + handle->numFds, handle->numInts*sizeof(int));

    struct android_wlegl *android_wlegl_wrapper = (struct android_wlegl *) wl_proxy_create_wrapper(android_wlegl);
    wl_proxy_set_queue((struct wl_proxy *) android_wlegl_wrapper, queue);

    wlegl_handle = android_wlegl_create_handle(android_wlegl_wrapper, handle->numFds, &ints);

    wl_array_release(&ints);

    for (int i = 0; i < handle->numFds; i++) {
        android_wlegl_handle_add_fd(wlegl_handle, handle->data[i]);
    }

    wlbuffer = android_wlegl_create_buffer(android_wlegl_wrapper,
            width, height, stride,
            format, (uint32_t)usage, wlegl_handle);

    android_wlegl_handle_destroy(wlegl_handle);

    wl_proxy_wrapper_destroy(android_wlegl_wrapper);

    struct wl_display *display_wrapper = (struct wl_display *) wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy *) display_wrapper, queue);

    creation_callback = wl_display_sync(display_wrapper);
    wl_callback_add_listener(creation_callback, &buffer_create_sync_listener, &creation_callback);

    wl_proxy_wrapper_destroy(display_wrapper);
}

void WaylandNativeWindow::resize(unsigned int width, unsigned int height)
{
    lock();
    this->m_defaultWidth = m_width = width;
    this->m_defaultHeight = m_height = height;
    unlock();
}

void WaylandNativeWindow::resize_callback(struct wl_egl_window *egl_window, void *)
{
    TRACE("%dx%d",egl_window->width,egl_window->height);
    ((WaylandNativeWindow *) egl_window->driver_private)->resize(egl_window->width, egl_window->height);
}

void WaylandNativeWindow::destroy_window_callback(void *data)
{
    WaylandNativeWindow *native = (WaylandNativeWindow*)data;

    native->lock();
    native->m_window = 0;
    native->unlock();
}

void WaylandNativeWindow::lock()
{
    pthread_mutex_lock(&this->mutex);
}

void WaylandNativeWindow::unlock()
{
    pthread_mutex_unlock(&this->mutex);
}

void
WaylandNativeWindow::sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
    int *done = static_cast<int *>(data);

    *done = 1;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    WaylandNativeWindow::sync_callback
};

static void check_fatal_error(struct wl_display *display)
{
    int error = wl_display_get_error(display);

    if (error == 0)
        return;

    fprintf(stderr, "Wayland display got fatal error %i: %s\n", error, strerror(error));

    if (errno != 0)
        fprintf(stderr, "Additionally, errno was set to %i: %s\n", errno, strerror(errno));

    fprintf(stderr, "The display is now unusable, aborting.\n");
    abort();
}

#ifdef WANT_LINDROID_DRM
static void registry_global_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    auto **dmabuf_ptr = static_cast<zwp_linux_dmabuf_v1 **>(data);
    if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
        *dmabuf_ptr = static_cast<zwp_linux_dmabuf_v1 *>(
            wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, 3));
    }
}

static const struct wl_registry_listener registry_listener = {
    registry_global_handler,
    nullptr
};
#endif

WaylandNativeWindow::WaylandNativeWindow(struct wl_egl_window *window,
                                         struct wl_display *display,
                                         android_wlegl *wlegl)
    : m_android_wlegl(wlegl)
{
    HYBRIS_TRACE_BEGIN("wayland-platform", "create_window", "");
    this->m_window = window;
    this->m_window->driver_private = (void *) this;
    this->m_display = display;
    this->m_width = window->width;
    this->m_height = window->height;
    this->m_defaultWidth = window->width;
    this->m_defaultHeight = window->height;
    this->m_window->resize_callback = resize_callback;
    this->m_window->destroy_window_callback = destroy_window_callback;
    this->frame_callback = NULL;
    this->wl_queue = wl_display_create_queue(display);
    this->wl_dpy_wrapper = (struct wl_display *) wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy *) wl_dpy_wrapper, wl_queue);
    this->wl_surface_wrapper = (struct wl_surface *) wl_proxy_create_wrapper(m_window->surface);
    wl_proxy_set_queue((struct wl_proxy *) wl_surface_wrapper, wl_queue);
    this->m_format = 1;

    const_cast<int&>(ANativeWindow::minSwapInterval) = 0;
    const_cast<int&>(ANativeWindow::maxSwapInterval) = 1;
    // This is the default as per the EGL documentation
    this->m_swap_interval = 1;

    m_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    m_queueReads = 0;
    m_freeBufs = 0;
    m_damage_rects = NULL;
    m_damage_n_rects = 0;
#ifdef WANT_LINDROID_DRM
    this->wl_dmabuf = NULL;

    if(!m_android_wlegl) {
        struct wl_registry *registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, &this->wl_dmabuf);
        wl_display_roundtrip(display);
        wl_registry_destroy(registry);
    }
#endif // WANT_LINDROID_DRM
    WaylandNativeWindow::setBufferCount(3);
    HYBRIS_TRACE_END("wayland-platform", "create_window", "");
}

WaylandNativeWindow::~WaylandNativeWindow()
{
    destroyBuffers();
    if (frame_callback)
        wl_callback_destroy(frame_callback);
    wl_proxy_wrapper_destroy(wl_surface_wrapper);
    wl_proxy_wrapper_destroy(wl_dpy_wrapper);
    wl_event_queue_destroy(wl_queue);
    if (m_window) {
        m_window->driver_private = NULL;
        m_window->resize_callback = NULL;
        m_window->destroy_window_callback = NULL;
    }
}

void WaylandNativeWindow::frame() {
    HYBRIS_TRACE_BEGIN("wayland-platform", "frame_event", "");

    this->frame_callback = NULL;

    HYBRIS_TRACE_END("wayland-platform", "frame_event", "");
}

// overloads from BaseNativeWindow
int WaylandNativeWindow::setSwapInterval(int interval) {
    TRACE("interval:%i", interval);

    if (interval < 0)
        interval = 0;
    if (interval > 1)
        interval = 1;

    HYBRIS_TRACE_BEGIN("wayland-platform", "swap_interval", "=%d", interval);

    lock();
    m_swap_interval = interval;
    unlock();

    HYBRIS_TRACE_END("wayland-platform", "swap_interval", "");

    return 0;
}

void WaylandNativeWindow::releaseBuffer(struct wl_buffer *buffer)
{
    std::list<WaylandNativeWindowBuffer *>::iterator it;

    it = fronted.begin();

    for (; it != fronted.end(); ++it)
    {
        if ((*it)->wlbuffer == buffer)
            break;
    }
    assert(it != fronted.end());



    WaylandNativeWindowBuffer* wnb = *it;
    fronted.erase(it);
    HYBRIS_TRACE_COUNTER("wayland-platform", "fronted.size", "%lu", fronted.size());

    for (it = m_bufList.begin(); it != m_bufList.end(); ++it)
    {
        if ((*it) == wnb)
            break;
    }
    assert(it != m_bufList.end());
    HYBRIS_TRACE_BEGIN("wayland-platform", "releaseBuffer", "-%p", wnb);
    wnb->busy = 0;

    ++m_freeBufs;
    HYBRIS_TRACE_COUNTER("wayland-platform", "m_freeBufs", "%i", m_freeBufs);
    for (it = m_bufList.begin(); it != m_bufList.end(); ++it)
    {
        (*it)->youngest = 0;
    }
    wnb->youngest = 1;


    HYBRIS_TRACE_END("wayland-platform", "releaseBuffer", "-%p", wnb);
}

int WaylandNativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer){
    WaylandNativeWindowBuffer *wnb = (WaylandNativeWindowBuffer*) buffer;
    (void)wnb;
    HYBRIS_TRACE_BEGIN("wayland-platform", "lockBuffer", "-%p", wnb);
    HYBRIS_TRACE_END("wayland-platform", "lockBuffer", "-%p", wnb);
    return NO_ERROR;
}

int WaylandNativeWindow::readQueue(bool block)
{
    int ret = 0;

    if (++m_queueReads == 1) {
        if (block) {
            ret = wl_display_dispatch_queue(m_display, wl_queue);
        } else {
            ret = wl_display_dispatch_queue_pending(m_display, wl_queue);
        }

        // all threads waiting on the false branch will wake and return now, so we
        // can safely set m_queueReads to 0 here instead of relying on every thread
        // to decrement it. This prevents a race condition when a thread enters readQueue()
        // before the one in this thread returns.
        // The new thread would go in the false branch, and there would be no thread in the
        // true branch, blocking the new thread and any other that will call readQueue in
        // the future.
        m_queueReads = 0;

        pthread_cond_broadcast(&cond);

        if (ret < 0) {
            TRACE("wl_display_dispatch_queue returned an error");
            check_fatal_error(m_display);
            return ret;
        }
    } else if (block) {
        while (m_queueReads > 0) {
            pthread_cond_wait(&cond, &mutex);
        }
    }

    return ret;
}

int WaylandNativeWindow::cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd){
    std::list<WaylandNativeWindowBuffer *>::iterator it;
    WaylandNativeWindowBuffer *wnb = (WaylandNativeWindowBuffer*) buffer;

    lock();
    HYBRIS_TRACE_BEGIN("wayland-platform", "cancelBuffer", "-%p", wnb);

    /* Check first that it really is our buffer */
    for (it = m_bufList.begin(); it != m_bufList.end(); ++it)
    {
        if ((*it) == wnb)
            break;
    }
    assert(it != m_bufList.end());

    wnb->busy = 0;
    ++m_freeBufs;
    HYBRIS_TRACE_COUNTER("wayland-platform", "m_freeBufs", "%i", m_freeBufs);

    for (it = m_bufList.begin(); it != m_bufList.end(); ++it)
    {
        (*it)->youngest = 0;
    }
    wnb->youngest = 1;

    if (m_queueReads != 0) {
        // Some thread is waiting on wl_display_dispatch_queue(), possibly waiting for a wl_buffer.release
        // event. Since we have now cancelled a buffer push an artificial event so that the dispatch returns
        // and the thread can notice the cancelled buffer. This means there is a delay of one roundtrip,
        // but I don't see other solution except having one dedicated thread for calling wl_display_dispatch_queue().
        wl_callback_destroy(wl_display_sync(wl_dpy_wrapper));
    }

    HYBRIS_TRACE_END("wayland-platform", "cancelBuffer", "-%p", wnb);
    unlock();

    return 0;
}

unsigned int WaylandNativeWindow::width() const {
    TRACE("value:%i", m_width);
    return m_width;
}

unsigned int WaylandNativeWindow::height() const {
    TRACE("value:%i", m_height);
    return m_height;
}

unsigned int WaylandNativeWindow::format() const {
    TRACE("value:%i", m_format);
    return m_format;
}

unsigned int WaylandNativeWindow::defaultWidth() const {
    TRACE("value:%i", m_defaultWidth);
    return m_defaultWidth;
}

unsigned int WaylandNativeWindow::defaultHeight() const {
    TRACE("value:%i", m_defaultHeight);
    return m_defaultHeight;
}

unsigned int WaylandNativeWindow::queueLength() const {
    TRACE("WARN: stub");
    return 1;
}

unsigned int WaylandNativeWindow::type() const {
    TRACE("");
#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=3 || ANDROID_VERSION_MAJOR>=5
    /* https://android.googlesource.com/platform/system/core/+/bcfa910611b42018db580b3459101c564f802552%5E!/ */
    return NATIVE_WINDOW_SURFACE;
#else
    return NATIVE_WINDOW_SURFACE_TEXTURE_CLIENT;
#endif
}

unsigned int WaylandNativeWindow::transformHint() const {
    TRACE("WARN: stub");
    return 0;
}

/*
 * returns the current usage of this window
 */
unsigned int WaylandNativeWindow::getUsage() const {
    return m_usage;
}

int WaylandNativeWindow::setBuffersFormat(int format) {
    lock();
    if (format != m_format)
    {
        TRACE("old-format:x%x new-format:x%x", m_format, format);
        m_format = format;
        /* Buffers will be re-allocated when dequeued */
    } else {
        TRACE("format:x%x", format);
    }
    unlock();
    return NO_ERROR;
}

void WaylandNativeWindow::destroyBuffer(WaylandNativeWindowBuffer* wnb)
{
    TRACE("wnb:%p", wnb);

    assert(wnb != NULL);

    int ret = 0;
    while (ret != -1 && wnb->creation_callback)
        ret = wl_display_dispatch_queue(m_display, wl_queue);

    if (wnb->creation_callback) {
        wl_callback_destroy(wnb->creation_callback);
        wnb->creation_callback = NULL;
    }

    if (wnb->wlbuffer)
        wl_buffer_destroy(wnb->wlbuffer);
    wnb->wlbuffer = NULL;
    wnb->common.decRef(&wnb->common);
    m_freeBufs--;
}

void WaylandNativeWindow::destroyBuffers()
{
    TRACE("");

    std::list<WaylandNativeWindowBuffer*>::iterator it = m_bufList.begin();
    for (; it!=m_bufList.end(); ++it)
    {
        destroyBuffer(*it);
    }
    m_bufList.clear();
    m_freeBufs = 0;
}

WaylandNativeWindowBuffer *WaylandNativeWindow::addBuffer() {
    WaylandNativeWindowBuffer *wnb;

#ifdef WANT_LINDROID_DRM
    if(!m_android_wlegl) {
#ifdef HYBRIS_VULKAN_WS
        wnb = new EvdiWaylandBuffer(m_width, m_height, m_format, m_usage, wl_queue, this->wl_dmabuf);
#else
        wnb = new DrmWaylandBuffer(m_width, m_height, m_format, m_usage, m_display, wl_queue, this->wl_dmabuf);
#endif
    } else {
#endif // WANT_LINDROID_DRM
#ifndef HYBRIS_NO_SERVER_SIDE_BUFFERS
        wnb = new ServerWaylandBuffer(m_width, m_height, m_format, m_usage, m_android_wlegl, wl_queue);
        wl_display_roundtrip_queue(m_display, wl_queue);
#else
        wnb = new ClientWaylandBuffer(m_width, m_height, m_format, m_usage);
#endif
#ifdef WANT_LINDROID_DRM
    }
#endif // WANT_LINDROID_DRM

    m_bufList.push_back(wnb);
    ++m_freeBufs;

    TRACE("wnb:%p width:%i height:%i format:x%x usage:x%x",
         wnb, wnb->width, wnb->height, wnb->format, wnb->usage);

    return wnb;
}

int WaylandNativeWindow::setBufferCount(int cnt) {
    TRACE("cnt:%d", cnt);

#ifdef HYBRIS_VULKAN_WS
    /*
     * A Wayland compositor keeps the most recently attached buffer as the
     * surface's current content and only releases it once a newer buffer is
     * attached, so one buffer of the pool is permanently unavailable.
     *
     * Android's swapchain dequeues every image up front in
     * vkCreateSwapchainKHR and expects all of setBufferCount()'s buffers to be
     * dequeueable. That holds the first time round, but on a swapchain
     * re-create the previous swapchain's last frame is still on screen, so the
     * driver comes up one buffer short and blocks in dequeueBuffer() waiting
     * for a release that only its own next attach could trigger.
     *
     * Keep a spare so the driver always gets the count it asked for.
     */
    cnt += 1;
#endif

    if ((int)m_bufList.size() == cnt)
        return NO_ERROR;

    lock();
    if ((int)m_bufList.size() > cnt) {
        /* Decreasing buffer count, remove from beginning */
        std::list<WaylandNativeWindowBuffer*>::iterator it = m_bufList.begin();
        for (int i = 0; i <= (int)m_bufList.size() - cnt; i++ )
        {
            destroyBuffer(*it);
            ++it;
            m_bufList.pop_front();
        }

    } else {
        /* Increasing buffer count, start from current size */
        for (int i = (int)m_bufList.size(); i < cnt; i++)
            (void)addBuffer();

    }

    unlock();

    return NO_ERROR;
}

int WaylandNativeWindow::setBuffersDimensions(int width, int height) {
    lock();
    if (m_width != width || m_height != height)
    {
        TRACE("old-size:%ix%i new-size:%ix%i", m_width, m_height, width, height);
        m_width = width;
        m_height = height;
        /* Buffers will be re-allocated when dequeued */
    } else {
        TRACE("size:%ix%i", width, height);
    }
    unlock();
    return NO_ERROR;
}

int WaylandNativeWindow::setUsage(uint64_t usage) {
    usage |= GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

    lock();
    if (usage != m_usage)
    {
        TRACE("old-usage:x%" PRIx64 " new-usage:x%" PRIx64, m_usage, usage);
        m_usage = usage;
        /* Buffers will be re-allocated when dequeued */
    } else {
        TRACE("usage:x%" PRIx64, usage);
    }
    unlock();
    return NO_ERROR;
}

#ifdef HYBRIS_NO_SERVER_SIDE_BUFFERS

void ClientWaylandBuffer::init(struct android_wlegl *android_wlegl,
                                     struct wl_display *display,
                                     struct wl_event_queue *queue)
{
    wlbuffer_from_native_handle(android_wlegl, display, queue);
}

#else // HYBRIS_NO_SERVER_SIDE_BUFFERS

static void ssb_ints(void *data, android_wlegl_server_buffer_handle *, wl_array *ints)
{
    ServerWaylandBuffer *wsb = static_cast<ServerWaylandBuffer *>(data);
    wl_array_copy(&wsb->ints, ints);
}

static void ssb_fd(void *data, android_wlegl_server_buffer_handle *, int fd)
{
    ServerWaylandBuffer *wsb = static_cast<ServerWaylandBuffer *>(data);
    int *ptr = (int *)wl_array_add(&wsb->fds, sizeof(int));
    *ptr = fd;
}

static void ssb_buffer(void *data, android_wlegl_server_buffer_handle *,
                       wl_buffer *buffer,
                       int32_t format,
                       int32_t stride)
{
    ServerWaylandBuffer *wsb = static_cast<ServerWaylandBuffer *>(data);

    native_handle_t *native;
    int numFds = wsb->fds.size / sizeof(int);
    int numInts = wsb->ints.size / sizeof(int32_t);

    native = native_handle_create(numFds, numInts);

    memcpy(&native->data[0], wsb->fds.data, wsb->fds.size);
    memcpy(&native->data[numFds], wsb->ints.data, wsb->ints.size);
    /* ownership of fds passed to native_handle_t */
    wsb->fds.size = 0;

    wsb->handle = NULL;
    wsb->format = format;
    wsb->stride = stride;

    int ret = hybris_gralloc_import_buffer(native, &wsb->handle);

    native_handle_close(native);
    native_handle_delete(native);

    if (ret) {
        fprintf(stderr,"failed to register buffer\n");
        return;
    }

    wsb->common.incRef(&wsb->common);
    wsb->m_buf = buffer;
}

static const struct android_wlegl_server_buffer_handle_listener server_handle_listener = {
    ssb_fd,
    ssb_ints,
    ssb_buffer,
};

ServerWaylandBuffer::ServerWaylandBuffer(unsigned int w,
                                         unsigned int h,
                                         int _format,
                                         uint64_t _usage,
                                         android_wlegl *android_wlegl,
                                         struct wl_event_queue *queue)
                   : WaylandNativeWindowBuffer()
                   , m_buf(0)
{
    ANativeWindowBuffer::width = w;
    ANativeWindowBuffer::height = h;
    usage = _usage;

    wl_array_init(&ints);
    wl_array_init(&fds);

    struct android_wlegl *android_wlegl_wrapper = (struct android_wlegl *) wl_proxy_create_wrapper(android_wlegl);
    wl_proxy_set_queue((struct wl_proxy *) android_wlegl_wrapper, queue);

    ssb = android_wlegl_get_server_buffer_handle(android_wlegl_wrapper, width, height, _format, (uint32_t)_usage);
    wl_proxy_set_queue((struct wl_proxy *) ssb, queue);
    android_wlegl_server_buffer_handle_add_listener(ssb, &server_handle_listener, this);

    wl_proxy_wrapper_destroy(android_wlegl_wrapper);
}

ServerWaylandBuffer::~ServerWaylandBuffer()
{
    if (m_buf)
        wl_buffer_destroy(m_buf);

    hybris_gralloc_release(handle, 1);
    wl_array_release(&ints);
    wl_array_release(&fds);
    android_wlegl_server_buffer_handle_destroy(ssb);
}

void ServerWaylandBuffer::init(android_wlegl *, wl_display *, wl_event_queue *queue)
{
    wlbuffer = m_buf;
    m_buf = 0;
    wl_proxy_set_queue((struct wl_proxy *) wlbuffer, queue);
}

#endif // HYBRIS_NO_SERVER_SIDE_BUFFERS

#ifdef WANT_LINDROID_DRM
DrmWaylandBuffer::DrmWaylandBuffer(
    unsigned int w, unsigned int h, int _format, uint64_t _usage,
    struct wl_display *display, struct wl_event_queue *queue,
    struct zwp_linux_dmabuf_v1 *dmabuf)
    : WaylandNativeWindowBuffer(), wl_dmabuf(dmabuf)
{
    width = w;
    height = h;
    format = _format;
    usage = _usage;

    int ret = hybris_gralloc_allocate(width, height, format, (uint32_t)usage, &handle, (uint32_t*)&stride);
    if (ret != 0) {
        fprintf(stderr, "lindroid-drm: failed to allocate gralloc buffer: %d\n", ret);
        abort();
    }

    common.incRef(&common);
}

void DrmWaylandBuffer::init(
    struct android_wlegl *android_wlegl,
    struct wl_display *display,
    struct wl_event_queue *queue)
{
    if (!wl_dmabuf) {
        HYBRIS_ERROR("lindroid-drm: Wayland zwp_linux_dmabuf_v1 not available!\n");
        abort();
    }

    auto* params = zwp_linux_dmabuf_v1_create_params(wl_dmabuf);
    const native_handle_t* nh = reinterpret_cast<const native_handle_t*>(handle);

    if (!nh) {
        fprintf(stderr, "lindroid-drm: NULL handle\n");
        abort();
    }

    for (int i = 0; i < nh->numFds; i++) {
        int plane_fd = fcntl(nh->data[i], F_DUPFD_CLOEXEC, 0);
        if (plane_fd < 0) {
            fprintf(stderr, "lindroid-drm: failed to dup plane %d fd: %s\n",
                    i, strerror(errno));
            abort();
        }
        zwp_linux_buffer_params_v1_add(params, plane_fd, i, 0, stride * 4, 0, 0);
    }

    size_t buffer_size = sizeof(int) * (4 + nh->numInts);
    std::vector<int> buffer(4 + nh->numInts);

    buffer[0] = -1;
    buffer[1] = nh->version;
    buffer[2] = nh->numFds;
    buffer[3] = nh->numInts;

    for (int i = 0; i < nh->numInts; i++)
        buffer[4 + i] = nh->data[nh->numFds + i];

    int meta_fd = memfd_create("lindroid-meta", MFD_CLOEXEC);
    if (ftruncate(meta_fd, buffer_size) != 0) {
        fprintf(stderr, "lindroid-drm: ftruncate failed\n");
        close(meta_fd);
        abort();
    }

    if (pwrite(meta_fd, buffer.data(), buffer_size, 0) != (ssize_t)buffer_size) {
        fprintf(stderr, "lindroid-drm: failed to write data into meta_fd\n");
        close(meta_fd);
        abort();
    }

    // passes to wayland-client, which closes it after sending.
    zwp_linux_buffer_params_v1_add(params, meta_fd, nh->numFds, 0, 1, 0, 0);

    uint32_t req_format = (format == HAL_PIXEL_FORMAT_RGBX_8888) ? GBM_FORMAT_XRGB8888 : GBM_FORMAT_ABGR8888;
    wlbuffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, req_format, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(wlbuffer), queue);
}

DrmWaylandBuffer::~DrmWaylandBuffer() {
    if (handle)
        hybris_gralloc_release(handle, 1);
}

#ifdef HYBRIS_VULKAN_WS

static int evdi_auth_magic(int fd, drm_magic_t magic)
{
    drm_auth_t auth;
    auth.magic = magic;
    if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth))
        return -errno;
    return 0;
}

static int lindroid_evdi_fd(void)
{
    static pthread_mutex_t evdi_mutex = PTHREAD_MUTEX_INITIALIZER;
    static int fd = -1;

    pthread_mutex_lock(&evdi_mutex);
    if (fd < 0) {
        const char *path = getenv("LINDROID_EVDI_CARD");
        if (!path)
            path = "/dev/dri/by-path/platform-evdi-lindroid.0-card";

        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            HYBRIS_ERROR("lindroid-drm: failed to open %s: %s\n", path, strerror(errno));
        } else if (evdi_auth_magic(fd, 0) != -EACCES) {
            if (ioctl(fd, DRM_IOCTL_DROP_MASTER, NULL) < 0) {
                HYBRIS_ERROR("lindroid-drm: failed to drop master on %s: %s\n",
                             path, strerror(errno));
                close(fd);
                fd = -1;
            }
        }
    }
    pthread_mutex_unlock(&evdi_mutex);

    return fd;
}

EvdiWaylandBuffer::EvdiWaylandBuffer(
    unsigned int w, unsigned int h, int _format, uint64_t _usage,
    struct wl_event_queue *queue, struct zwp_linux_dmabuf_v1 *dmabuf)
    : WaylandNativeWindowBuffer(), evdi_id(-1), wl_dmabuf(dmabuf)
{
    width = w;
    height = h;
    format = _format;
    usage = _usage;

    int fd = lindroid_evdi_fd();
    if (fd < 0) {
        HYBRIS_ERROR("lindroid-drm: no evdi node, cannot allocate buffer\n");
        abort();
    }

    uint32_t pixel_stride = 0;
    struct drm_evdi_gbm_create_buff create;
    memset(&create, 0, sizeof(create));
    create.width = w;
    create.height = h;
    create.format = (uint32_t)_format;
    create.id = &evdi_id;
    create.stride = &pixel_stride;

    if (ioctl(fd, DRM_IOCTL_EVDI_GBM_CREATE_BUFF, &create) < 0) {
        HYBRIS_ERROR("lindroid-drm: DRM_IOCTL_EVDI_GBM_CREATE_BUFF failed: %s\n",
                     strerror(errno));
        abort();
    }

    if (evdi_id <= 0 || pixel_stride == 0) {
        HYBRIS_ERROR("lindroid-drm: invalid CREATE_BUFF reply: id=%d stride=%u\n",
                     evdi_id, pixel_stride);
        abort();
    }

    stride = (int)pixel_stride;

    struct drm_evdi_gbm_get_buff get;
    memset(&get, 0, sizeof(get));
    get.id = evdi_id;
    get.native_handle = malloc(max_native_handle_size);
    if (!get.native_handle) {
        HYBRIS_ERROR("lindroid-drm: out of memory for native handle\n");
        abort();
    }

    if (ioctl(fd, DRM_IOCTL_EVDI_GBM_GET_BUFF, &get) < 0) {
        HYBRIS_ERROR("lindroid-drm: DRM_IOCTL_EVDI_GBM_GET_BUFF(%d) failed: %s\n",
                     evdi_id, strerror(errno));
        free(get.native_handle);
        abort();
    }

    native_handle_t *raw = (native_handle_t *)get.native_handle;
    const native_handle_t *imported = NULL;

    if (hybris_gralloc_import_buffer(raw, (buffer_handle_t *)&imported) == 0 && imported) {
        handle = imported;
        native_handle_close(raw);
        native_handle_delete(raw);
    } else {
        HYBRIS_ERROR("lindroid-drm: failed to import buffer %d, using raw handle\n",
                     evdi_id);
        handle = raw;
    }

    common.incRef(&common);
}

void EvdiWaylandBuffer::init(
    struct android_wlegl *android_wlegl,
    struct wl_display *display,
    struct wl_event_queue *queue)
{
    if (!wl_dmabuf) {
        HYBRIS_ERROR("lindroid-drm: Wayland zwp_linux_dmabuf_v1 not available!\n");
        abort();
    }

    auto *params = zwp_linux_dmabuf_v1_create_params(wl_dmabuf);

    uint32_t byte_stride = (uint32_t)stride * 4u;
    size_t size = (size_t)byte_stride * height;

    int id_fd = memfd_create("lindroid-evdi-id", MFD_CLOEXEC);
    if (id_fd < 0) {
        HYBRIS_ERROR("lindroid-drm: memfd_create failed: %s\n", strerror(errno));
        abort();
    }

    if (write(id_fd, &evdi_id, sizeof(int)) != (ssize_t)sizeof(int)) {
        HYBRIS_ERROR("lindroid-drm: failed to write evdi id into memfd\n");
        close(id_fd);
        abort();
    }

    if (ftruncate(id_fd, size) < 0) {
        HYBRIS_ERROR("lindroid-drm: ftruncate failed: %s\n", strerror(errno));
        close(id_fd);
        abort();
    }

    zwp_linux_buffer_params_v1_add(params, id_fd, 0, 0, byte_stride, 0, 0);

    uint32_t req_format = (format == HAL_PIXEL_FORMAT_RGBX_8888) ? GBM_FORMAT_XRGB8888
                                                                 : GBM_FORMAT_ABGR8888;
    wlbuffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, req_format, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    wl_proxy_set_queue(reinterpret_cast<struct wl_proxy *>(wlbuffer), queue);
}

EvdiWaylandBuffer::~EvdiWaylandBuffer()
{
    if (handle)
        hybris_gralloc_release(handle, 1);

    if (evdi_id > 0) {
        int fd = lindroid_evdi_fd();
        struct drm_evdi_gbm_del_buff del;
        memset(&del, 0, sizeof(del));
        del.id = evdi_id;
        if (fd >= 0 && ioctl(fd, DRM_IOCTL_EVDI_GBM_DEL_BUFF, &del) < 0)
            HYBRIS_ERROR("lindroid-drm: DRM_IOCTL_EVDI_GBM_DEL_BUFF(%d) failed: %s\n",
                         evdi_id, strerror(errno));
        evdi_id = -1;
    }
}
#endif // HYBRIS_VULKAN_WS
#endif // WANT_LINDROID_DRM
// vim: noai:ts=4:sw=4:ss=4:expandtab
