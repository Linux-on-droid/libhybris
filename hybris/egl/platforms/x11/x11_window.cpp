/****************************************************************************************
 **
 ** Copyright (C) 2013 Jolla Ltd.
 ** Contact: Carsten Munk <carsten.munk@jollamobile.com>
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
#include "x11_window.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "logging.h"
#include <eglhybris.h>
#include "xcb_drihybris.h"

#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=2 || ANDROID_VERSION_MAJOR>=5
extern "C" {
#include <sync/sync.h>
}
#endif

void X11NativeWindow::resize(unsigned int width, unsigned int height)
{
    lock();
    this->m_defaultWidth = this->m_width = width;
    this->m_defaultHeight = this->m_height = height;
    unlock();
}

void X11NativeWindow::lock()
{
    pthread_mutex_lock(&this->mutex);
}

void X11NativeWindow::unlock()
{
    pthread_mutex_unlock(&this->mutex);
}

X11NativeWindow::X11NativeWindow(Display* xl_display, Window xl_window,
                                 bool drihybris)
{
    int wayland_ok;

    HYBRIS_TRACE_BEGIN("x11-platform", "create_window", "");
    this->m_window = xl_window;
    this->m_display = xl_display;
    this->m_connection = XGetXCBConnection(xl_display);
    this->m_image = 0;
    this->m_useShm = true;
    this->m_format = drihybris ? HAL_PIXEL_FORMAT_RGBA_8888 : HAL_PIXEL_FORMAT_BGRA_8888;
    this->m_haveDRIHybris = drihybris;

    const_cast<int&>(ANativeWindow::minSwapInterval) = 0;
    const_cast<int&>(ANativeWindow::maxSwapInterval) = 1;
    // This is the default as per the EGL documentation
    this->m_swap_interval = 1;
    
    TRACE("getting X11 window information");

    XWindowAttributes window_attributes;
    XGetWindowAttributes(m_display, m_window, &window_attributes);

    TRACE("window x=%d y=%d width=%d height=%d depth=%d",
        window_attributes.x,
        window_attributes.y,
        window_attributes.width,
        window_attributes.height,
        window_attributes.depth);

    m_width = window_attributes.width;
    m_height = window_attributes.height;
    m_depth = window_attributes.depth;

    const char *env = getenv("HYBRIS_X11_FORCE_WIDTH");
    if (env != NULL)
    {
        m_width = atoi(env);
        TRACE("forced width=%d", m_width);
    }

    env = getenv("HYBRIS_X11_FORCE_HEIGHT");
    if (env != NULL)
    {
        m_height = atoi(env);
        TRACE("forced height=%d", m_height);
    }

    m_defaultWidth = m_width;
    m_defaultHeight = m_height;

    env = getenv("HYBRIS_X11_DISABLE_SHM");
    if (env != NULL)
    {
        m_useShm = false;
        TRACE("won't use MIT-SHM");
    }

    XGCValues gcvalues;
    m_gc = XCreateGC(m_display, m_window, 0, &gcvalues);

    m_xcb_gc = xcb_generate_id(m_connection);
    xcb_create_gc(m_connection, m_xcb_gc, m_window, 0, 0);

    m_specialEvent = 0;
    registerForPresentEvents();

    m_usage=GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    m_queueReads = 0;
    m_freeBufs = 0;
    m_damage_rects = NULL;
    m_damage_n_rects = 0;
    m_lastBuffer = 0;
    setBufferCount(3);
    HYBRIS_TRACE_END("x11-platform", "create_window", "");
}

X11NativeWindow::~X11NativeWindow()
{
    std::list<X11NativeWindowBuffer *>::iterator it = m_bufList.begin();
    destroyBuffers();
}

// overloads from BaseNativeWindow
int X11NativeWindow::setSwapInterval(int interval) {
    TRACE("interval:%i", interval);

    if (interval < 0)
        interval = 0;
    if (interval > 1)
        interval = 1;

    HYBRIS_TRACE_BEGIN("x11-platform", "swap_interval", "=%d", interval);

    lock();
    m_swap_interval = interval;
    unlock();

    HYBRIS_TRACE_END("x11-platform", "swap_interval", "");

    return 0;
}

int X11NativeWindow::dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd){
    HYBRIS_TRACE_BEGIN("x11-platform", "dequeueBuffer", "");

    X11NativeWindowBuffer *wnb=NULL;
    TRACE("%p", buffer);

    readQueue(false);

    HYBRIS_TRACE_BEGIN("x11-platform", "dequeueBuffer_wait_for_buffer", "");

    HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);

    while (m_freeBufs == 0) {
        HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);
        readQueue(true);
    }

    lock();

    std::list<X11NativeWindowBuffer *>::iterator it = m_bufList.begin();
    for (; it != m_bufList.end(); it++)
    {
        if ((*it)->busy)
            continue;
        if ((*it)->youngest == 1)
            continue;
        break;
    }

    if (it==m_bufList.end()) {
        HYBRIS_TRACE_BEGIN("x11-platform", "dequeueBuffer_worst_case_scenario", "");
        HYBRIS_TRACE_END("x11-platform", "dequeueBuffer_worst_case_scenario", "");

        it = m_bufList.begin();
        for (; it != m_bufList.end() && (*it)->busy; it++)
        {}

    }
    if (it==m_bufList.end()) {
        unlock();
        HYBRIS_TRACE_BEGIN("x11-platform", "dequeueBuffer_no_free_buffers", "");
        HYBRIS_TRACE_END("x11-platform", "dequeueBuffer_no_free_buffers", "");
        TRACE("%p: no free buffers", buffer);
        return NO_ERROR;
    }

    wnb = *it;
    assert(wnb!=NULL);
    HYBRIS_TRACE_END("x11-platform", "dequeueBuffer_wait_for_buffer", "");

    /* If the buffer doesn't match the window anymore, re-allocate */
    if (wnb->width != m_width || wnb->height != m_height
        || wnb->format != m_format || wnb->usage != m_usage)
    {
        TRACE("wnb:%p,win:%p %i,%i %i,%i x%x,x%x x%x,x%x",
            wnb,m_window,
            wnb->width,m_width, wnb->height,m_height,
            wnb->format,m_format, wnb->usage,m_usage);
        destroyBuffer(wnb);
        m_bufList.erase(it);
        wnb = addBuffer();
    }

    wnb->busy = 1;
    *buffer = wnb;
    queue.push_back(wnb);
    --m_freeBufs;

    HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);
    HYBRIS_TRACE_BEGIN("x11-platform", "dequeueBuffer_gotBuffer", "-%p", wnb);
    HYBRIS_TRACE_END("x11-platform", "dequeueBuffer_gotBuffer", "-%p", wnb);
    HYBRIS_TRACE_END("x11-platform", "dequeueBuffer_wait_for_buffer", "");

    unlock();
    return NO_ERROR;
}

int X11NativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer){
    X11NativeWindowBuffer *wnb = (X11NativeWindowBuffer*) buffer;
    HYBRIS_TRACE_BEGIN("x11-platform", "lockBuffer", "-%p", wnb);
    HYBRIS_TRACE_END("x11-platform", "lockBuffer", "-%p", wnb);
    return NO_ERROR;
}

int X11NativeWindow::readQueue(bool block)
{
    int ret = 0;

    if (++m_queueReads == 1) {
        if (m_specialEvent) {
            xcb_generic_event_t    *ev;

            if (!block)
            {
                while ((ev = xcb_poll_for_special_event(m_connection,
                                                        m_specialEvent)) != NULL) {
                    xcb_present_generic_event_t *ge = (xcb_present_generic_event_t *) ev;
                    handlePresentEvent(ge);
                }
            }
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

    } else if (block) {
        while (m_queueReads > 0) {
            pthread_cond_wait(&cond, &mutex);
        }
    }

    return ret;
}

void X11NativeWindow::prepareSwap(EGLint *damage_rects, EGLint damage_n_rects)
{
    lock();
    m_damage_rects = damage_rects;
    m_damage_n_rects = damage_n_rects;
    unlock();
}

void X11NativeWindow::finishSwap()
{
    static int serial = 0;
    int ret = 0;
    lock();

    X11NativeWindowBuffer *wnb = queue.front();
    if (!wnb) {
        wnb = m_lastBuffer;
    } else {
        queue.pop_front();
    }
    assert(wnb);
    m_lastBuffer = wnb;
    wnb->busy = 1;

    // fronted.push_back(wnb);

    m_damage_rects = NULL;
    m_damage_n_rects = 0;
    unlock();

    if (m_haveDRIHybris) {
        if (wnb->pixmap == 0)
            wnb->pixmap_from_buffer(m_connection, m_window);

        xcb_present_pixmap(m_connection,
                            m_window,
                            wnb->pixmap,
                            (uint32_t) serial++,
                            0,                                    /* valid */
                            0,                                    /* update */
                            0,                                    /* x_off */
                            0,                                    /* y_off */
                            None,                                 /* target_crtc */
                            None,
                            NULL,
                            XCB_PRESENT_OPTION_NONE,
                            0,
                            0,
                            0, 0, NULL);
        xcb_flush(m_connection);

        lock();

        ++m_freeBufs;
        HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);

        std::list<X11NativeWindowBuffer *>::iterator it;
        for (it = m_bufList.begin(); it != m_bufList.end(); it++)
        {
            (*it)->youngest = 0;
        }
        wnb->youngest = 1;
        wnb->busy = 0;

        unlock();
    } else {
        copyToX11(wnb);
    }
}

static int debugenvchecked = 0;

int X11NativeWindow::queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    X11NativeWindowBuffer *wnb = (X11NativeWindowBuffer*) buffer;
    int ret = 0;

    HYBRIS_TRACE_BEGIN("x11-platform", "queueBuffer", "-%p", wnb);
    lock();

    if (debugenvchecked == 0)
    {
        if (getenv("HYBRIS_WAYLAND_DUMP_BUFFERS") != NULL)
            debugenvchecked = 2;
        else
            debugenvchecked = 1;
    }
    if (debugenvchecked == 2)
    {
        HYBRIS_TRACE_BEGIN("x11-platform", "queueBuffer_dumping_buffer", "-%p", wnb);
        hybris_dump_buffer_to_file(wnb->getNativeBuffer());
        HYBRIS_TRACE_END("x11-platform", "queueBuffer_dumping_buffer", "-%p", wnb);

    }

#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=2 || ANDROID_VERSION_MAJOR>=5
    HYBRIS_TRACE_BEGIN("x11-platform", "queueBuffer_waiting_for_fence", "-%p", wnb);
    if (fenceFd >= 0)
    {
        sync_wait(fenceFd, -1);
        close(fenceFd);
    }
    HYBRIS_TRACE_END("x11-platform", "queueBuffer_waiting_for_fence", "-%p", wnb);
#endif

    HYBRIS_TRACE_COUNTER("x11-platform", "fronted.size", "%i", fronted.size());
    HYBRIS_TRACE_END("x11-platform", "queueBuffer", "-%p", wnb);

    unlock();

    return NO_ERROR;
}

int X11NativeWindow::cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd){
    std::list<X11NativeWindowBuffer *>::iterator it;
    X11NativeWindowBuffer *wnb = (X11NativeWindowBuffer*) buffer;

    lock();
    HYBRIS_TRACE_BEGIN("x11-platform", "cancelBuffer", "-%p", wnb);

    /* Check first that it really is our buffer */
    for (it = m_bufList.begin(); it != m_bufList.end(); it++)
    {
        if ((*it) == wnb)
            break;
    }
    assert(it != m_bufList.end());

    wnb->busy = 0;
    ++m_freeBufs;
    HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);

    for (it = m_bufList.begin(); it != m_bufList.end(); it++)
    {
        (*it)->youngest = 0;
    }
    wnb->youngest = 1;

    if (m_queueReads != 0) {
        // Some thread is waiting on wl_display_dispatch_queue(), possibly waiting for a wl_buffer.release
        // event. Since we have now cancelled a buffer push an artificial event so that the dispatch returns
        // and the thread can notice the cancelled buffer. This means there is a delay of one roundtrip,
        // but I don't see other solution except having one dedicated thread for calling wl_display_dispatch_queue().
        //wl_callback_destroy(wl_display_sync(m_display));
    }

    HYBRIS_TRACE_END("x11-platform", "cancelBuffer", "-%p", wnb);
    unlock();

    return 0;
}

unsigned int X11NativeWindow::width() const {
    TRACE("value:%i", m_width);
    return m_width;
}

unsigned int X11NativeWindow::height() const {
    TRACE("value:%i", m_height);
    return m_height;
}

unsigned int X11NativeWindow::format() const {
    TRACE("value:%i", m_format);
    return m_format;
}

unsigned int X11NativeWindow::defaultWidth() const {
    TRACE("value:%i", m_defaultWidth);
    return m_defaultWidth;
}

unsigned int X11NativeWindow::defaultHeight() const {
    TRACE("value:%i", m_defaultHeight);
    return m_defaultHeight;
}

unsigned int X11NativeWindow::queueLength() const {
    TRACE("WARN: stub");
    return 1;
}

unsigned int X11NativeWindow::type() const {
    TRACE("");
#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=3 || ANDROID_VERSION_MAJOR>=5
    /* https://android.googlesource.com/platform/system/core/+/bcfa910611b42018db580b3459101c564f802552%5E!/ */
    return NATIVE_WINDOW_SURFACE;
#else
    return NATIVE_WINDOW_SURFACE_TEXTURE_CLIENT;
#endif
}

unsigned int X11NativeWindow::transformHint() const {
    TRACE("WARN: stub");
    return 0;
}

/*
 * returns the current usage of this window
 */
unsigned int X11NativeWindow::getUsage() const {
    return m_usage;
}

int X11NativeWindow::setBuffersFormat(int format) {
    if (format != m_format)
    {
        TRACE("old-format:x%x new-format:x%x", m_format, format);
        m_format = format;
        /* Buffers will be re-allocated when dequeued */
    } else {
        TRACE("format:x%x", format);
    }
    return NO_ERROR;
}

void X11NativeWindow::destroyBuffer(X11NativeWindowBuffer* wnb)
{
    TRACE("wnb:%p", wnb);

    assert(wnb != NULL);

    int ret = 0;

    wnb->common.decRef(&wnb->common);
    m_freeBufs--;
}

void X11NativeWindow::destroyBuffers()
{
    TRACE("");

    std::list<X11NativeWindowBuffer*>::iterator it = m_bufList.begin();
    for (; it!=m_bufList.end(); ++it)
    {
        destroyBuffer(*it);
        it = m_bufList.erase(it);
    }
    m_bufList.clear();
    m_freeBufs = 0;
}

X11NativeWindowBuffer *X11NativeWindow::addBuffer() {

    X11NativeWindowBuffer *wnb;

    wnb = new ClientX11Buffer(m_width, m_height, m_format, m_usage, m_depth);
    m_bufList.push_back(wnb);
    ++m_freeBufs;

    TRACE("wnb:%p width:%i height:%i format:x%x usage:x%x",
        wnb, wnb->width, wnb->height, wnb->format, wnb->usage);

    return wnb;
}

int X11NativeWindow::setBufferCount(int cnt) {
    int start = 0;

    TRACE("cnt:%d", cnt);

    if (m_bufList.size() == cnt)
        return NO_ERROR;

    lock();

    if (m_bufList.size() > cnt) {
        /* Decreasing buffer count, remove from beginning */
        std::list<X11NativeWindowBuffer*>::iterator it = m_bufList.begin();
        for (int i = 0; i <= m_bufList.size() - cnt; i++ )
        {
            destroyBuffer(*it);
            ++it;
            m_bufList.pop_front();
        }

    } else {
        /* Increasing buffer count, start from current size */
        for (int i = m_bufList.size(); i < cnt; i++)
            X11NativeWindowBuffer *unused = addBuffer();

    }

    unlock();

    return NO_ERROR;
}

int X11NativeWindow::setBuffersDimensions(int width, int height) {
    if (m_width != width || m_height != height)
    {
        TRACE("old-size:%ix%i new-size:%ix%i", m_width, m_height, width, height);
        m_width = width;
        m_height = height;
        /* Buffers will be re-allocated when dequeued */
    } else {
        TRACE("size:%ix%i", width, height);
    }
    return NO_ERROR;
}

int X11NativeWindow::setUsage(int usage) {
//     if ((usage | GRALLOC_USAGE_HW_TEXTURE) != m_usage)
//     {
//         TRACE("old-usage:x%x new-usage:x%x", m_usage, usage);
//         m_usage = usage | GRALLOC_USAGE_HW_TEXTURE;
//         /* Buffers will be re-allocated when dequeued */
//     } else {
//         TRACE("usage:x%x", usage);
//     }
    return NO_ERROR;
}

void X11NativeWindow::copyToX11(X11NativeWindowBuffer *wnb) {
    int ret;
    void *vaddr;
    std::list<X11NativeWindowBuffer *>::iterator it;

    ret = hybris_gralloc_lock(wnb->handle, wnb->usage, 0, 0, wnb->width, wnb->height, &vaddr);
    TRACE("wnb:%p gralloc lock returns %i", wnb, ret);
    TRACE("wnb:%p lock to vaddr %p", wnb, vaddr);
    TRACE("wnb:%p width=%d stride=%d height=%d format=%d", wnb, wnb->width, wnb->stride, wnb->height, wnb->format);

    if (!m_image)
    {
        if (m_useShm)
        {
            m_image = XShmCreateImage(m_display,
                        CopyFromParent,
                        m_depth,
                        ZPixmap, 0, &m_shminfo, wnb->stride, wnb->height);

            m_shminfo.shmid = shmget(IPC_PRIVATE,
                m_image->bytes_per_line * m_image->height,
                IPC_CREAT|0777);

            m_shminfo.shmaddr = m_image->data = (char *)shmat(m_shminfo.shmid, 0, 0);
            m_shminfo.readOnly = 0;

            TRACE("m_shminfo.shmaddr %p", m_shminfo.shmaddr);

            XShmAttach(m_display, &m_shminfo);
        }
        else
        {
            m_image = XCreateImage(m_display,
                                CopyFromParent,
                                m_depth,
                                ZPixmap, 0, (char *)vaddr, wnb->stride, wnb->height, 32, 0);
        }
    }


    if (m_useShm)
    {
        memcpy(m_image->data, vaddr, m_image->bytes_per_line * m_image->height);
        hybris_gralloc_unlock(wnb->handle);
        XShmPutImage(m_display, m_window, m_gc, m_image, 0, 0, 0, 0, m_width, m_height, 0);
    }
    else
    {
        m_image->data = (char *)vaddr;
        XPutImage(m_display, m_window, m_gc, m_image, 0, 0, 0, 0, m_width, m_height);
        hybris_gralloc_unlock(wnb->handle);
    }

    lock();

    ++m_freeBufs;
    HYBRIS_TRACE_COUNTER("x11-platform", "m_freeBufs", "%i", m_freeBufs);
    for (it = m_bufList.begin(); it != m_bufList.end(); it++)
    {
        (*it)->youngest = 0;
    }
    wnb->youngest = 1;
    wnb->busy = 0;

    unlock();
}

void X11NativeWindow::registerForPresentEvents()
{
    const xcb_query_extension_reply_t *extension;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;

    xcb_prefetch_extension_data(m_connection, &xcb_present_id);
    extension = xcb_get_extension_data(m_connection, &xcb_present_id);
    if (!(extension && extension->present))
        return;

    m_specialEventId = xcb_generate_id(m_connection);
    m_specialEvent = xcb_register_for_special_xge(m_connection,
                            &xcb_present_id, m_specialEventId, NULL);

    cookie = xcb_present_select_input_checked(m_connection,
            m_specialEventId, m_window,
            XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
            XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY);

    error = xcb_request_check(m_connection, cookie);
    if (error) {
        return;
    }
    TRACE("registered for present events\n");
}

void X11NativeWindow::handlePresentEvent(xcb_present_generic_event_t *ge)
{
    switch (ge->evtype) {
    case XCB_PRESENT_CONFIGURE_NOTIFY: {
        xcb_present_configure_notify_event_t *ce = (xcb_present_configure_notify_event_t *) ge;
        TRACE("XCB_PRESENT_CONFIGURE_NOTIFY: %dx%d\n", ce->width, ce->height);
        resize(ce->width, ce->height);
        break;
    }
    }
}

void X11NativeWindowBuffer::pixmap_from_buffer(xcb_connection_t *connection, xcb_drawable_t drawable)
{
    int32_t * fds;
    fds = (int32_t *)calloc(handle->numFds, sizeof(int));
    for (int i = 0; i < handle->numFds; i++) {
        fds[i] = dup(handle->data[i]);
    }

    xcb_drihybris_pixmap_from_buffer_checked(connection,
                               (pixmap = xcb_generate_id(connection)),
                               drawable,
                               stride * height * 4,
                               this->width, height, stride,
                               windowDepth, 32,
                               handle->numInts,
                               handle->numFds,
                               (const uint32_t *)(handle->data + handle->numFds),
                               (const int32_t *)fds);
    xcb_flush(connection);
    free(fds);
}

// vim: noai:ts=4:sw=4:ss=4:expandtab
