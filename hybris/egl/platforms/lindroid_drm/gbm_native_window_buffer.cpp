#include <assert.h>
#include <cstdlib>
#include <fcntl.h>

#include "logging.h"

#include "gbm_native_window_buffer.h"
#include "evdi_drm.h"

extern int evdi_open(char *device_path);
extern int evdi_get_native_handle_t(int native_handle_id, native_handle_t **handle, bool import);
extern int drm_fd;
extern struct gbm_device *gbm_dev;

void GbmNativeWindowBuffer::init(unsigned int w, unsigned int h, int _format, uint64_t _usage, gbm_surface *_surface) {
    int native_handle_id = -1;
    int ret = 0;
    ANativeWindowBuffer::width = w;
    ANativeWindowBuffer::height = h;
    ANativeWindowBuffer::format = _format;
    ANativeWindowBuffer::usage = _usage;
    surface = _surface;

    if (drm_fd < 0) {
        HYBRIS_ERROR("DRM device was never open\n");
        drm_fd = evdi_open("/dev/dri/by-path/platform-evdi-lindroid.0-card");
        gbm_dev = gbm_create_device(drm_fd);
    }

    if (!gbm_dev) {
        HYBRIS_ERROR("GBM device was never created\n");
        abort();
    }

    // Map opaque HAL formats to opaque GBM formats
    uint32_t req_format = (_format == HAL_PIXEL_FORMAT_RGBX_8888) ? GBM_FORMAT_XRGB8888 : GBM_FORMAT_ABGR8888;
    bo = (gbm_hybris_bo *)gbm_bo_create(gbm_dev, w, h, req_format, GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    if (!bo) {
        HYBRIS_ERROR("Failed to create GBM BO\n");
        abort();
    }

    dmabuf_fd = gbm_bo_get_fd(&bo->base);
    if (dmabuf_fd < 0) {
        HYBRIS_ERROR("Failed to export GBM BO as DMA-BUF");
        gbm_bo_destroy(&bo->base);
        abort();
    }

    if (fcntl(dmabuf_fd, F_GETFD) == -1) {
        HYBRIS_ERROR("Fatal: Invalid or closed file descriptor: %d", dmabuf_fd);
        abort();
    }

    if (lseek(dmabuf_fd, 0, SEEK_SET) == -1) {
        HYBRIS_ERROR("Fatal: Failed to seek fd: %d, do fd come from non lindroid driver?", dmabuf_fd);
        abort();
    }

    if (read(dmabuf_fd, &native_handle_id, sizeof(int)) != sizeof(int)) {
        HYBRIS_ERROR("Fatal: failed to read fd: %d", dmabuf_fd);
        abort();
    }

    ret = evdi_get_native_handle_t(native_handle_id, (native_handle_t**)&handle, true);
    if(ret) {
        HYBRIS_ERROR("Failed to import bo\n");
        abort();
    }

    stride = gbm_bo_get_stride(&bo->base) / 4;
}

GbmNativeWindowBuffer::~GbmNativeWindowBuffer() {
    if(dmabuf_fd)
        close(dmabuf_fd);

    if(handle)
        native_handle_close(handle);

    if(bo)
        gbm_bo_destroy(&bo->base);
}
