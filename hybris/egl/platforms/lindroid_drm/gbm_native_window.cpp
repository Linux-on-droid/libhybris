#include "gbm_native_window.h"
#include <cstdlib>

void GbmNativeWindow::lock()
{
    pthread_mutex_lock(&this->mutex);
}

void GbmNativeWindow::unlock()
{
    pthread_mutex_unlock(&this->mutex);
}

static int hal_format_from_gbm(uint32_t gbm_format)
{
    switch (gbm_format) {
    case GBM_FORMAT_ABGR8888:
        return HAL_PIXEL_FORMAT_RGBA_8888;
    case GBM_FORMAT_XBGR8888:
        return HAL_PIXEL_FORMAT_RGBX_8888;
    case GBM_FORMAT_XRGB8888:
        return HAL_PIXEL_FORMAT_RGBX_8888;
    case GBM_FORMAT_RGB888:
        return HAL_PIXEL_FORMAT_RGB_888;
    case GBM_FORMAT_RGB565:
        return HAL_PIXEL_FORMAT_RGB_565;
    case GBM_FORMAT_ARGB8888:
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case GBM_FORMAT_GR88:
        return HAL_PIXEL_FORMAT_YV12;
    case GBM_FORMAT_ABGR16161616F:
        return HAL_PIXEL_FORMAT_RGBA_FP16;
    case GBM_FORMAT_ABGR2101010:
        return HAL_PIXEL_FORMAT_RGBA_1010102;
    default:
        return HAL_PIXEL_FORMAT_RGBA_8888;
    }
}

GbmNativeWindow::GbmNativeWindow(gbm_surface *surface, uint64_t usage)
        : m_gbm(nullptr),
          m_surface(surface),
          m_width(0),
          m_height(0),
          m_defaultWidth(0),
          m_defaultHeight(0),
          m_hal_format(0),
          m_gbm_format(0),
          m_usage(usage) {
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    if (m_surface) {
        m_gbm = m_surface->gbm;
        if (!m_gbm) {
            HYBRIS_ERROR("Invalid GBM surface passed to GbmNativeWindow");
            abort();
        }
        m_width = m_defaultWidth = m_surface->v0.width;
        m_height = m_defaultHeight = m_surface->v0.height;
        m_gbm_format = m_surface->v0.format
                     ? (int)m_surface->v0.format
                     : GBM_FORMAT_ABGR8888;
    }

    if (m_surface) {
        gbm_hybris_surface *hsurf =
            reinterpret_cast<gbm_hybris_surface *>(m_surface);
        unsigned int i;

        hsurf->front_bo = nullptr;
        hsurf->bo_count = 0;
        hsurf->locked_count = 0;

        for (i = 0; i < 16; ++i) {
            hsurf->bo[i] = nullptr;
            hsurf->locked[i] = nullptr;
        }
    }

    if (!m_gbm_format)
        m_gbm_format = GBM_FORMAT_ABGR8888;

    m_hal_format = hal_format_from_gbm(m_gbm_format);

    const_cast<int&>(ANativeWindow::minSwapInterval) = 0;
    const_cast<int&>(ANativeWindow::maxSwapInterval) = 1;
    setBufferCount(3);
}

GbmNativeWindow::~GbmNativeWindow() {
    lock();

    gbm_hybris_surface *hsurf = hybrisSurface();
    if (hsurf) {
        unsigned int i;
        hsurf->front_bo = nullptr;
        hsurf->bo_count = 0;
        hsurf->locked_count = 0;
        for (i = 0; i < 16; ++i) {
            hsurf->bo[i] = nullptr;
            hsurf->locked[i] = nullptr;
        }
    }

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
    std::list<GbmNativeWindowBuffer *>::iterator it;

    lock();

    for (;;) {
        it = m_bufList.begin();

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

    if (!m_gbm && m_surface)
        m_gbm = m_surface->gbm;

    if ((m_width == 0 || m_height == 0) && m_surface) {
        m_width = m_surface->v0.width;
        m_height = m_surface->v0.height;
    }

    if (!m_gbm_format && m_surface && m_surface->v0.format)
        m_gbm_format = (int)m_surface->v0.format;

    if (!m_gbm) {
        unlock();
        return -ENOMEM;
    }

    if (m_width == 0 || m_height == 0) {
        unlock();
        return -EINVAL;
    }

    if (!bnb->bo ||
        bnb->width  != m_width ||
        bnb->height != m_height ||
        bnb->format != m_hal_format ||
        bnb->usage  != m_usage) {
        m_queue.remove(bnb);
        m_fronted.remove(bnb);
        delete bnb;
        bnb = new GbmNativeWindowBuffer();
        bnb->init(m_width, m_height, m_hal_format, m_usage, m_surface);
        *it = bnb;
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
    bnb->youngest = true;
    bnb->fronted = false;

     for (std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin(); it != m_bufList.end(); ++it) {
        if (*it != bnb)
            (*it)->youngest = false;
    }

    resyncSurfaceBoList();
    pthread_cond_broadcast(&cond);
    unlock();

    return NO_ERROR;
}

void GbmNativeWindow::finishSwap() {
    lock();

    if (m_queue.empty()) {
        unlock();
        return;
    }

    gbm_hybris_surface *hsurf = hybrisSurface();
    if (!hsurf) {
        GbmNativeWindowBuffer *stale = m_queue.front();
        m_queue.pop_front();

        if (stale) {
            stale->busy = false;
            stale->queued = false;
            stale->fronted = false;
        }

        pthread_cond_broadcast(&cond);
        unlock();
        return;
    }

    GbmNativeWindowBuffer *bnb = m_queue.front();
    m_queue.pop_front();

    if (!bnb) {
        pthread_cond_broadcast(&cond);
        unlock();
        return;
    }

    if (!bnb->bo) {
        bnb->busy = false;
        bnb->queued = false;
        bnb->fronted = false;
        pthread_cond_broadcast(&cond);
        unlock();
        return;
    }

    for (std::list<GbmNativeWindowBuffer *>::iterator it = m_bufList.begin();
         it != m_bufList.end(); ++it) {
        (*it)->youngest = false;
        (*it)->fronted = false;
    }
    bnb->queued = false;
    bnb->fronted = true;
    bnb->youngest = true;
    bnb->busy = false;

    m_fronted.clear();
    m_fronted.push_back(bnb);

    hsurf->front_bo = bnb->bo;
    resyncSurfaceBoList();

    pthread_cond_broadcast(&cond);

    unlock();
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
    return m_hal_format;
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
    m_hal_format = format;
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
    if (!m_surface)
        return nullptr;

    return reinterpret_cast<gbm_hybris_surface*>(m_surface);
}

void GbmNativeWindow::resyncSurfaceBoList() {
    gbm_hybris_surface* hsurf = hybrisSurface();
    if (!hsurf)
        return;

    hsurf->bo_count = 0;
    for (unsigned i = 0; i < 16; ++i)
        hsurf->bo[i] = nullptr;

    for (std::list<GbmNativeWindowBuffer*>::const_iterator it = m_bufList.begin(); it != m_bufList.end() && hsurf->bo_count < 16; ++it) {
        if ((*it)->bo) {
            hsurf->bo[hsurf->bo_count++] = (*it)->bo;
        }
    }

    hsurf->front_bo = nullptr;
    for (std::list<GbmNativeWindowBuffer*>::const_iterator it = m_fronted.begin();
         it != m_fronted.end(); ++it) {
        if ((*it)->fronted && (*it)->bo) {
            hsurf->front_bo = (*it)->bo;
            break;
        }
    }
    }
