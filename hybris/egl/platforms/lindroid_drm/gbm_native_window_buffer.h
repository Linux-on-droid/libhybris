#pragma once
#include "gbm_backend_abi.h"
#include "eglnativewindowbase.h"

#include <gbm.h>
#include <EGL/egl.h>

struct gbm_hybris_surface {
    struct gbm_surface base;
    struct gbm_hybris_bo *front_bo;
    bool front_locked;
    struct gbm_hybris_bo *bo[16];
    unsigned int bo_count;
};

struct gbm_hybris_bo {
   struct gbm_bo base;
   int evdi_lindroid_buff_id;
};

struct GbmNativeWindowBuffer : public BaseNativeWindowBuffer {
    GbmNativeWindowBuffer() {};
    ~GbmNativeWindowBuffer();

    void init(unsigned int w, unsigned int h, int _format, uint64_t _usage, gbm_surface *_surface);

    gbm_hybris_bo *bo;
    gbm_surface *surface;
    int dmabuf_fd;
    bool busy = false;
    bool queued = false;
    bool fronted = false;
    bool youngest = false;
};
