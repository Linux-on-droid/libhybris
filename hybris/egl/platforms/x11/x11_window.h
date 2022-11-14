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

#ifndef X11_WINDOW_H
#define X11_WINDOW_H
#include "eglnativewindowbase.h"
#include <linux/fb.h>
#include <hybris/gralloc/gralloc.h>

extern "C" {
#include <X11/Xlib-xcb.h>
#include <xcb/present.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <pthread.h>
}

#include <list>
#include <deque>

class X11NativeWindowBuffer : public BaseNativeWindowBuffer
{
public:
    X11NativeWindowBuffer() : busy(0), youngest(0), other(0) {}
    X11NativeWindowBuffer(ANativeWindowBuffer *other)
    {
        ANativeWindowBuffer::width = other->width;
        ANativeWindowBuffer::height = other->height;
        ANativeWindowBuffer::format = other->format;
        ANativeWindowBuffer::usage = other->usage;
        ANativeWindowBuffer::handle = other->handle;
        ANativeWindowBuffer::stride = other->stride;

        this->busy = 0;
        this->other = other;
        this->youngest = 0;
        this->pixmap = 0;
    }

    int busy;
    int youngest;
    ANativeWindowBuffer *other;
    int windowDepth;
    xcb_pixmap_t pixmap;

    void pixmap_from_buffer(xcb_connection_t *connection, xcb_drawable_t drawable);
};

class ClientX11Buffer : public X11NativeWindowBuffer
{
friend class X11NativeWindow;
protected:
    ClientX11Buffer() {}

    ClientX11Buffer(unsigned int width,
                    unsigned int height,
                    unsigned int format,
                    unsigned int usage,
                    unsigned int windowDepth)
    {
        // Base members
        ANativeWindowBuffer::width = width;
        ANativeWindowBuffer::height = height;
        ANativeWindowBuffer::format = format;
        ANativeWindowBuffer::usage = usage;

        this->busy = 0;
        this->other = NULL;
        int alloc_ok = hybris_gralloc_allocate(this->width ? this->width : 1,
                this->height ? this->height : 1,
                this->format, this->usage,
                &this->handle, (uint32_t*)&this->stride);
        assert(alloc_ok == 0);
        this->youngest = 0;
        this->common.incRef(&this->common);

        this->windowDepth = windowDepth;
        this->pixmap = 0;
    }

    ~ClientX11Buffer()
    {
        hybris_gralloc_release(this->handle, 1);
    }

protected:
    void* vaddr;

public:

};

class X11NativeWindow : public EGLBaseNativeWindow {
public:
    X11NativeWindow(Display* xl_display, Window xl_window, bool drihybris);
    ~X11NativeWindow();

    void lock();
    void unlock();
    void frame();
    void resize(unsigned int width, unsigned int height);
    void releaseBuffer(struct wl_buffer *buffer);

    virtual int setSwapInterval(int interval);
    void prepareSwap(EGLint *damage_rects, EGLint damage_n_rects);
    void finishSwap();

protected:
    // overloads from BaseNativeWindow
    virtual int dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd);
    virtual int lockBuffer(BaseNativeWindowBuffer* buffer);
    virtual int queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd);
    virtual int cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd);
    virtual unsigned int type() const;
    virtual unsigned int width() const;
    virtual unsigned int height() const;
    virtual unsigned int format() const;
    virtual unsigned int defaultWidth() const;
    virtual unsigned int defaultHeight() const;
    virtual unsigned int queueLength() const;
    virtual unsigned int transformHint() const;
    virtual unsigned int getUsage() const;
    // perform calls
    virtual int setUsage(uint64_t usage);
    virtual int setBuffersFormat(int format);
    virtual int setBuffersDimensions(int width, int height);
    virtual int setBufferCount(int cnt);

private:
    X11NativeWindowBuffer *addBuffer();
    void destroyBuffer(X11NativeWindowBuffer *);
    void destroyBuffers();
    int readQueue(bool block);

    void copyToX11(X11NativeWindowBuffer *wnb);
    void tryEnableDRIHybris();
    void registerForPresentEvents();
    void handlePresentEvent(xcb_present_generic_event_t *ge);

    std::list<X11NativeWindowBuffer *> m_bufList;
    std::list<X11NativeWindowBuffer *> fronted;
    std::list<X11NativeWindowBuffer *> posted;
    std::list<X11NativeWindowBuffer *> post_registered;
    std::deque<X11NativeWindowBuffer *> queue;

    Display* m_display;
    Window m_window;
    XImage *m_image;
    XShmSegmentInfo m_shminfo;
    GC m_gc;

    xcb_connection_t *m_connection;
    xcb_gcontext_t m_xcb_gc;
    xcb_present_event_t m_specialEventId;
    xcb_special_event_t *m_specialEvent;

    bool m_useShm;
    bool m_haveDRIHybris;
    
    X11NativeWindowBuffer *m_lastBuffer;
    unsigned int m_width;
    unsigned int m_height;
    unsigned int m_depth;
    unsigned int m_format;
    unsigned int m_defaultWidth;
    unsigned int m_defaultHeight;
    unsigned int m_usage;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int m_queueReads;
    int m_freeBufs;
    EGLint *m_damage_rects, m_damage_n_rects;
    int m_swap_interval;
};

#endif
// vim: noai:ts=4:sw=4:ss=4:expandtab
