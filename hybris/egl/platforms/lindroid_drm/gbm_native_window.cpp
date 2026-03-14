#include "gbm_native_window.h"

void GbmNativeWindow::lock()
{
    pthread_mutex_lock(&this->mutex);
}

void GbmNativeWindow::unlock()
{
    pthread_mutex_unlock(&this->mutex);
}

GbmNativeWindow::GbmNativeWindow(gbm_surface *surface, uint64_t usage)
        : m_gbm(surface->gbm), m_surface(surface), m_width(surface->v0.width), m_height(surface->v0.height),
        m_defaultWidth(surface->v0.width), m_defaultHeight(surface->v0.height), m_format(surface->v0.format), m_usage(usage) {
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    const_cast<int&>(ANativeWindow::minSwapInterval) = 0;
    const_cast<int&>(ANativeWindow::maxSwapInterval) = 1;

    setBufferCount(3);
}

GbmNativeWindow::~GbmNativeWindow() {
    lock();

    for (std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin();
        it != m_bufList.end(); ++it) {
        delete *it;
    }
    m_bufList.clear();

    unlock();

    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
}

void GbmNativeWindow::resize(uint32_t width, uint32_t height) {
    lock();
    m_width = m_defaultWidth = width;
    m_height = m_defaultHeight = height;
    unlock();
}

int GbmNativeWindow::setSwapInterval(int interval) {
    lock();
    if (interval < 0)
        interval = 0;
    if (interval > 1)
        interval = 1;
    m_swapInterval = interval;
    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd) {
    GbmNativeWindowBuffer *bnb = nullptr;

    lock();

    for (;;) {
        std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin();

        for (; it != m_bufList.end(); ++it) {
            if ((*it)->busy)
                continue;
            if ((*it)->youngest)
                continue;
            break;
        }

        if (it == m_bufList.end()) {
            for (it = m_bufList.begin(); it != m_bufList.end(); ++it) {
                if (!(*it)->busy)
                    break;
            }
        }

        if (it != m_bufList.end()) {
            bnb = *it;
            break;
        }

        pthread_cond_wait(&cond, &mutex);
    }

    if (!bnb->bo ||
        bnb->width  != m_width ||
        bnb->height != m_height ||
        bnb->format != m_format ||
        bnb->usage  != m_usage) {
	delete bnb;
        bnb = new GbmNativeWindowBuffer();
        bnb->init(m_width, m_height, m_format, m_usage, m_surface);
        resyncSurfaceBoList();
    }

    bnb->busy = true;
    bnb->queued = false;
    bnb->fronted = false;

    *buffer = bnb;
    if (fenceFd)
        *fenceFd = -1;

    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::queueBuffer(BaseNativeWindowBuffer *buffer, int fenceFd) {
    GbmNativeWindowBuffer *bnb = static_cast<GbmNativeWindowBuffer *>(buffer);

    lock();

#if ANDROID_VERSION_MAJOR >= 4 && ANDROID_VERSION_MINOR >= 2 || ANDROID_VERSION_MAJOR >= 5
    if (fenceFd >= 0) {
        unlock();
        sync_wait(fenceFd, -1);
        close(fenceFd);
        lock();
     }
#else
     (void)fenceFd;
#endif
    if (!bnb->queued) {
        bnb->queued = true;
        m_queue.push_back(bnb);
    }
    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::cancelBuffer(BaseNativeWindowBuffer *buffer, int fenceFd) {
        GbmNativeWindowBuffer *bnb = static_cast<GbmNativeWindowBuffer *>(buffer);

#if ANDROID_VERSION_MAJOR >= 4 && ANDROID_VERSION_MINOR >= 2 || ANDROID_VERSION_MAJOR >= 5
    if (fenceFd >= 0) {
        sync_wait(fenceFd, -1);
        close(fenceFd);
    }
#else
    (void)fenceFd;
#endif
    lock();
    m_queue.remove(bnb);
    m_fronted.remove(bnb);
    bnb->busy = false;
    bnb->queued = false;
    bnb->bo = nullptr;
    bnb->youngest = true;

     for (std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin(); it != m_bufList.end(); ++it) {
        if (*it != bnb)
            (*it)->youngest = false;
    }

    resyncSurfaceBoList();
    pthread_cond_broadcast(&cond);
    unlock();

    return NO_ERROR;
}

int GbmNativeWindow::finishSwap() {
    lock();

    if (m_queue.empty()) {
        unlock();
        return 0;
    }

    GbmNativeWindowBuffer *bnb = m_queue.front();
    m_queue.pop_front();

    if (!bnb->bo) {
        bnb->busy = false;
        bnb->queued = false;
        pthread_cond_broadcast(&cond);
        unlock();
        return -1;
    }

    bnb->queued = false;
    bnb->fronted = true;

    for (std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin();
         it != m_bufList.end(); ++it) {
        (*it)->youngest = false;
    }
    bnb->youngest = true;
    bnb->busy = false;
    m_fronted.push_back(bnb);

    hybrisSurface()->front_bo = bnb->bo;
    hybrisSurface()->front_locked = false;

    unlock();
    return 0;
}

int GbmNativeWindow::lockBuffer(BaseNativeWindowBuffer *buffer) {
    (void)buffer;
    return NO_ERROR;
}

unsigned int GbmNativeWindow::width() const {
    return m_width;
}

unsigned int GbmNativeWindow::height() const {
    return m_height;
}

unsigned int GbmNativeWindow::format() const {
    return m_format;
}

unsigned int GbmNativeWindow::defaultWidth() const {
    return m_defaultWidth;
}

unsigned int GbmNativeWindow::defaultHeight() const {
    return m_defaultHeight;
}

unsigned int GbmNativeWindow::queueLength() const {
    return 1;
}

unsigned int GbmNativeWindow::type() const {
#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=3 || ANDROID_VERSION_MAJOR>=5
    /* https://android.googlesource.com/platform/system/core/+/bcfa910611b42018db580b3459101c564f802552%5E!/ */
    return NATIVE_WINDOW_SURFACE;
#else
    return NATIVE_WINDOW_SURFACE_TEXTURE_CLIENT;
#endif
}

unsigned int GbmNativeWindow::transformHint() const {
    return 0;
}

unsigned int GbmNativeWindow::getUsage() const {
    return m_usage;
}

int GbmNativeWindow::setBuffersFormat(int format) {
    lock();
    m_format = format;
    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::setBufferCount(int cnt) {
    lock();

    while ((int)m_bufList.size() > cnt) {
        GbmNativeWindowBuffer *buf = m_bufList.front();
        m_queue.remove(buf);
        m_fronted.remove(buf);

        delete buf;
        m_bufList.pop_front();
    }

    while ((int)m_bufList.size() < cnt) {
       m_bufList.push_back(new GbmNativeWindowBuffer());
    }

    resyncSurfaceBoList();
    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::setBuffersDimensions(int width, int height) {
    lock();
    m_width = width;
    m_height = height;
    unlock();
    return NO_ERROR;
}

int GbmNativeWindow::setUsage(uint64_t usage) {
    lock();
    m_usage = usage;
    unlock();
    return NO_ERROR;
}

gbm_hybris_surface* GbmNativeWindow::hybrisSurface() const
{
    return reinterpret_cast<gbm_hybris_surface*>(m_surface);
}

void GbmNativeWindow::resyncSurfaceBoList() {
    gbm_hybris_surface* hsurf = hybrisSurface();

    hsurf->bo_count = 0;
    for (unsigned i = 0; i < 16; ++i)
        hsurf->bo[i] = nullptr;

    for (std::list<GbmNativeWindowBuffer*>::const_iterator it = m_bufList.begin(); it != m_bufList.end() && hsurf->bo_count < 16; ++it) {
        if ((*it)->bo) {
            hsurf->bo[hsurf->bo_count++] = (*it)->bo;
        }
    }

    hsurf->front_bo = nullptr;
    for (std::list<GbmNativeWindowBuffer*>::const_iterator it = m_fronted.begin(); it != m_fronted.end(); ++it) {
        if ((*it)->fronted && (*it)->bo) {
            hsurf->front_bo = (*it)->bo;
        }
    }
}

