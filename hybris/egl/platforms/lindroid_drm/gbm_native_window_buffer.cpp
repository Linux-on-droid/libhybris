#include <assert.h>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <hardware/gralloc.h>

#include "logging.h"

#include "gbm_native_window_buffer.h"
#include "evdi_drm.h"

extern int evdi_open(char *device_path);
extern int evdi_get_native_handle_t(int native_handle_id, native_handle_t **handle, bool import);
extern int drm_fd;
extern uint32_t get_gbm_pixel_format(int hal_format);

void GbmNativeWindowBuffer::init(unsigned int w, unsigned int h, int _format, uint64_t _usage, gbm_surface *_surface, gbm_device *_gbm) {
    if (bo) {
        gbm_bo_destroy(&bo->base);
        bo = nullptr;
    }

    if (handle) {
        native_handle_close(handle);
        native_handle_delete(const_cast<native_handle_t *>(handle));
        handle = nullptr;
    }

    if (dmabuf_fd >= 0) {
        close(dmabuf_fd);
        dmabuf_fd = -1;
    }

    int native_handle_id = -1;
    int ret = 0;
    ANativeWindowBuffer::width = w;
    ANativeWindowBuffer::height = h;
    format = _format;
    usage = _usage;
    surface = _surface;

    if (!_gbm) {
        HYBRIS_ERROR("GBM device was not provided for this surface\n");
        abort();
    }

    if (_gbm->v0.fd < 0) {
        HYBRIS_ERROR("GBM device has an invalid fd\n");
        abort();
    }

    uint32_t req_format = get_gbm_pixel_format(_format);
    bo = (gbm_hybris_bo *)gbm_bo_create(_gbm, w, h, req_format, GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
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
    if (dmabuf_fd >= 0)
        close(dmabuf_fd);

    if (handle) {
        native_handle_close(handle);
        native_handle_delete(const_cast<native_handle_t *>(handle));
        handle = nullptr;
    }

    if (bo) {
        gbm_bo_destroy(&bo->base);
        bo = nullptr;
    }

    surface = nullptr;
    dmabuf_fd = -1;
}
