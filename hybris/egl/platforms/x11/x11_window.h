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
#include "nativewindowbase.h"
#include <linux/fb.h>
#include <hardware/gralloc.h>
extern "C" {
#include <X11/Xlib.h>
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
    }

    int busy;
    int youngest;
    ANativeWindowBuffer *other;

    virtual void init(struct android_wlegl *android_wlegl,
                    struct wl_display *display,
                    struct wl_event_queue *queue) {}
};

class ClientX11Buffer : public X11NativeWindowBuffer
{
friend class X11NativeWindow;
protected:
    ClientX11Buffer()
        : m_alloc(0)
    {}

    ClientX11Buffer(alloc_device_t* alloc_device,
                            unsigned int width,
                            unsigned int height,
                            unsigned int format,
                            unsigned int usage)
    {
        // Base members
        ANativeWindowBuffer::width = width;
        ANativeWindowBuffer::height = height;
        ANativeWindowBuffer::format = format;
        ANativeWindowBuffer::usage = usage;

        this->busy = 0;
        this->other = NULL;
        this->m_alloc = alloc_device;
        int alloc_ok = this->m_alloc->alloc(this->m_alloc,
                this->width ? this->width : 1, this->height ? this->height : 1,
                this->format, this->usage,
                &this->handle, &this->stride);
        assert(alloc_ok == 0);
        this->youngest = 0;
        this->common.incRef(&this->common);
    }

    ~ClientX11Buffer()
    {
        if (this->m_alloc)
            m_alloc->free(m_alloc, this->handle);
    }

    void init(struct android_wlegl *android_wlegl,
                                    struct wl_display *display,
                                    struct wl_event_queue *queue);

protected:
    void* vaddr;
    alloc_device_t* m_alloc;

public:

};

class X11NativeWindow : public BaseNativeWindow {
public:
    X11NativeWindow(Display* xl_display, Window xl_window, alloc_device_t* alloc, gralloc_module_t* gralloc);
    ~X11NativeWindow();

    void lock();
    void unlock();
    void frame();
    void resize(unsigned int width, unsigned int height);
    void releaseBuffer(struct wl_buffer *buffer);

    virtual int setSwapInterval(int interval);
    void prepareSwap(EGLint *damage_rects, EGLint damage_n_rects);
    void finishSwap();
    void copyToX11(X11NativeWindowBuffer *wnb);

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
    virtual int setUsage(int usage);
    virtual int setBuffersFormat(int format);
    virtual int setBuffersDimensions(int width, int height);
    virtual int setBufferCount(int cnt);

private:
    X11NativeWindowBuffer *addBuffer();
    void destroyBuffer(X11NativeWindowBuffer *);
    void destroyBuffers();
    int readQueue(bool block);

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
    bool m_useShm;
    
    X11NativeWindowBuffer *m_lastBuffer;
    unsigned int m_width;
    unsigned int m_height;
    unsigned int m_format;
    unsigned int m_defaultWidth;
    unsigned int m_defaultHeight;
    unsigned int m_usage;
    struct android_wlegl *m_android_wlegl;
    alloc_device_t* m_alloc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int m_queueReads;
    int m_freeBufs;
    EGLint *m_damage_rects, m_damage_n_rects;
    int m_swap_interval;
    gralloc_module_t *m_gralloc;
};

#endif
// vim: noai:ts=4:sw=4:ss=4:expandtab
