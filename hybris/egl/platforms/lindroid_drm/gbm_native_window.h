#pragma once
#include <gbm.h>
#include <EGL/egl.h>
#include <pthread.h>
#include <stdint.h>
#include <list>
#include <unordered_map>

#include "eglnativewindowbase.h"
#include "logging.h"

#include "evdi_drm.h"
#include "gbm_backend_abi.h"
#include "gbm_native_window_buffer.h"

#if ANDROID_VERSION_MAJOR >= 4 && ANDROID_VERSION_MINOR >= 2 || ANDROID_VERSION_MAJOR >= 5
extern "C" {
#include <sync/sync.h>
}
#endif

class GbmNativeWindow : public EGLBaseNativeWindow {
public:
    GbmNativeWindow(gbm_surface *surface, uint64_t usage = 0);
    ~GbmNativeWindow();

    void lock();
    void unlock();
    void resize(unsigned int width, unsigned int height);
    void prepareSwap(EGLint *damage_rects, EGLint damage_n_rects) override;

    int setSwapInterval(int interval);
    void finishSwap() override;

protected:
    virtual int dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd);
    virtual int lockBuffer(BaseNativeWindowBuffer *buffer);
    virtual int queueBuffer(BaseNativeWindowBuffer *buffer, int fenceFd);
    virtual int cancelBuffer(BaseNativeWindowBuffer *buffer, int fenceFd);
    virtual unsigned int type() const;
    virtual unsigned int width() const;
    virtual unsigned int height() const;
    virtual unsigned int format() const;
    virtual unsigned int defaultWidth() const;
    virtual unsigned int defaultHeight() const;
    virtual unsigned int queueLength() const;
    virtual unsigned int transformHint() const;
    virtual unsigned int getUsage() const;
    virtual int setUsage(uint64_t usage);
    virtual int setBuffersFormat(int format);
    virtual int setBuffersDimensions(int width, int height);
    virtual int setBufferCount(int cnt);

private:
    gbm_hybris_surface* hybrisSurface() const;
    bool isBufferLockedBySurface(const GbmNativeWindowBuffer *buffer) const;
    void resyncSurfaceBoList();

    gbm_device *m_gbm = nullptr;
    gbm_surface *m_surface = nullptr;

    unsigned int m_width = 0;
    unsigned int m_height = 0;
    unsigned int m_defaultWidth = 0;
    unsigned int m_defaultHeight = 0;
    int m_hal_format = 0;
    int m_gbm_format = 0;
    uint64_t m_usage = 0;
    int m_swapInterval = 1;

    mutable pthread_mutex_t mutex{};
    pthread_cond_t cond{};

    std::list<GbmNativeWindowBuffer *> m_bufList;
    std::list<GbmNativeWindowBuffer *> m_queue;
    std::list<GbmNativeWindowBuffer *> m_fronted;
};
