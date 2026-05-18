#pragma once
#include "gbm_backend_abi.h"
#include "eglnativewindowbase.h"

#include <gbm.h>
#include <stdint.h>
#include <EGL/egl.h>

struct gbm_hybris_surface {
    struct gbm_surface base;
    struct gbm_hybris_bo *front_bo;
    struct gbm_hybris_bo *bo[16];
    unsigned int bo_count;
    struct gbm_hybris_bo *locked[16];
    unsigned int locked_count;
};

struct gbm_hybris_bo {
   struct gbm_bo base;
   int evdi_lindroid_buff_id;
};

struct GbmNativeWindowBuffer : public BaseNativeWindowBuffer {
    GbmNativeWindowBuffer()
        : bo(nullptr),
          surface(nullptr),
          dmabuf_fd(-1),
          busy(false),
          queued(false),
          fronted(false),
          youngest(false) {
        handle = nullptr;
        width = 0;
        height = 0;
        format = 0;
        usage = 0;
        stride = 0;
    }
    ~GbmNativeWindowBuffer();

    void init(unsigned int w, unsigned int h, int _format, uint64_t _usage,
              gbm_surface *_surface);

    gbm_hybris_bo *bo;
    gbm_surface *surface;
    int dmabuf_fd;
    bool busy;
    bool queued;
    bool fronted;
    bool youngest;
};
