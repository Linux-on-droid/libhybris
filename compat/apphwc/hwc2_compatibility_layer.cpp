#include <cstdlib>
#include <unordered_map>

#include <ui/Fence.h>

#include "hwc2_compatibility_layer.h"

#include <aidl/android/hardware/graphics/common/HardwareBuffer.h>
#include <aidl/android/hardware/graphics/common/HardwareBufferDescription.h>
#include <aidl/vendor/lindroid/composer/BnComposerCallback.h>
#include <aidl/vendor/lindroid/composer/IComposer.h>
#include <aidlcommonsupport/NativeHandle.h>

#include <android/binder_manager.h>

namespace aidl {
namespace vendor {
namespace lindroid {
namespace composer {

class ComposerCallbackImpl : public BnComposerCallback {
public:
    ComposerCallbackImpl(HWC2EventListener *listener) : listener(listener) {}

    ndk::ScopedAStatus onVsyncReceived(int32_t in_sequenceId, int64_t in_display, int64_t in_timestamp) {
        listener->on_vsync_received(listener, in_sequenceId, static_cast<hwc2_display_t>(in_display), in_timestamp);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus onHotplugReceived(int32_t in_sequenceId, int64_t in_display, bool in_connected, bool in_primaryDisplay) {
        listener->on_hotplug_received(listener, in_sequenceId, static_cast<hwc2_display_t>(in_display), in_connected, in_primaryDisplay);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus onRefreshReceived(int32_t in_sequenceId, int64_t in_display, int64_t /* in_timestamp */) {
        listener->on_refresh_received(listener, in_sequenceId, static_cast<hwc2_display_t>(in_display));
        return ndk::ScopedAStatus::ok();
    }

private:
    HWC2EventListener *listener;
};

} // namespace composer
} // namespace lindroid
} // namespace vendor
} // namespace aidl

using namespace android;

using aidl::android::hardware::common::NativeHandle;
using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::HardwareBuffer;
using aidl::android::hardware::graphics::common::HardwareBufferDescription;
using aidl::android::hardware::graphics::common::PixelFormat;
using aidl::vendor::lindroid::composer::ComposerCallbackImpl;
using aidl::vendor::lindroid::composer::DisplayConfiguration;
using aidl::vendor::lindroid::composer::IComposer;

struct hwc2_compat_device
{
    std::shared_ptr<IComposer> self;
    std::shared_ptr<ComposerCallbackImpl> listener;
};

struct hwc2_compat_display
{
    hwc2_compat_device *device;
    hwc2_display_t id;
};

struct hwc2_compat_layer
{
    hwc2_compat_display *display;
};

struct hwc2_compat_out_fences
{
    std::unordered_map<hwc2_display_t, android::sp<android::Fence>> fences;
};

hwc2_compat_device_t* hwc2_compat_device_new(bool useVrComposer)
{
    hwc2_compat_device_t *device = new hwc2_compat_device_t();
    if (!device)
        return nullptr;

    ndk::SpAIBinder binder(AServiceManager_waitForService("vendor.lindroid.composer"));
    device->self = IComposer::fromBinder(binder);

    return device;
}

void hwc2_compat_device_register_callback(hwc2_compat_device_t *device,
                                          HWC2EventListener* listener,
                                          int composerSequenceId /*unused*/)
{
    device->listener = ndk::SharedRefBase::make<ComposerCallbackImpl>(listener);
    device->self->registerCallback(device->listener);
}

void hwc2_compat_device_on_hotplug(hwc2_compat_device_t* device,
                                    hwc2_display_t displayId, bool connected)
{
    device->self->onHotplug(static_cast<int64_t>(displayId), connected);
}

hwc2_compat_display_t* hwc2_compat_device_get_display_by_id(
                            hwc2_compat_device_t *device, hwc2_display_t id)
{
    hwc2_compat_display_t *display = (hwc2_compat_display_t*) malloc(
        sizeof(hwc2_compat_display_t));
    if (!display)
        return nullptr;

    display->device = device;

    device->self->requestDisplay(static_cast<int64_t>(id));

    return display;
}

void hwc2_compat_device_destroy_display(hwc2_compat_device_t* device,
                                        hwc2_compat_display_t* display)
{
    free(display);
}

HWC2DisplayConfig* hwc2_compat_display_get_active_config(
                            hwc2_compat_display_t* display)
{
    HWC2DisplayConfig* config = (HWC2DisplayConfig*) malloc(
        sizeof(HWC2DisplayConfig));

    DisplayConfiguration activeConfig;
    display->device->self->getActiveConfig(static_cast<int64_t>(display->id), &activeConfig);

    config->id = static_cast<hwc2_config_t>(activeConfig.configId);
    config->display = static_cast<hwc2_display_t>(activeConfig.displayId);
    config->width = activeConfig.width;
    config->height = activeConfig.height;
    config->vsyncPeriod = activeConfig.vsyncPeriod;
    config->dpiX = activeConfig.dpi.x;
    config->dpiY = activeConfig.dpi.y;

    return config;
}

hwc2_error_t hwc2_compat_display_accept_changes(hwc2_compat_display_t* display)
{
    display->device->self->acceptChanges(static_cast<int64_t>(display->id));
    return HWC2_ERROR_NONE;
}

hwc2_compat_layer_t* hwc2_compat_display_create_layer(hwc2_compat_display_t* display)
{
    hwc2_compat_layer_t *layer = new hwc2_compat_layer_t();
    if (!layer)
        return nullptr;

    layer->display = display;

    return layer;
}

void hwc2_compat_display_destroy_layer(hwc2_compat_display_t* display,
                                       hwc2_compat_layer_t* layer)
{
    delete layer;
}

hwc2_error_t hwc2_compat_display_get_release_fences(hwc2_compat_display_t* display,
                                       hwc2_compat_out_fences_t** outFences)
{
    hwc2_compat_out_fences_t *fences = new struct hwc2_compat_out_fences;
    ndk::ScopedFileDescriptor out_fence;

    display->device->self->getReleaseFence(display->id, &out_fence);
    fences->fences[display->id] = new android::Fence(out_fence.get());
    *outFences = fences;

    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_display_present(hwc2_compat_display_t* display,
                                    int32_t* outPresentFence)
{
    ndk::ScopedFileDescriptor out_fence;
    display->device->self->present(display->id, &out_fence);

    *outPresentFence = out_fence.get();

    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_display_set_client_target(hwc2_compat_display_t* display,
                                            uint32_t slot,
                                            struct ANativeWindowBuffer* buffer,
                                            const int32_t acquireFenceFd,
                                            android_dataspace_t dataspace)
{
    int32_t error = HWC2_ERROR_NONE;
    HardwareBufferDescription description = {
        .width = static_cast<int32_t>(buffer->width),
        .height = static_cast<int32_t>(buffer->height),
        .layers = static_cast<int32_t>(1),
        .format = static_cast<PixelFormat>(buffer->format),
        .usage = static_cast<BufferUsage>(buffer->usage),
        .stride = static_cast<int32_t>(buffer->stride),
    };
    HardwareBuffer out = {
        .description = description,
        .handle = dupToAidl(buffer->handle),
    };

    display->device->self->setBuffer(static_cast<int64_t>(display->id), out, acquireFenceFd, &error);

    return static_cast<hwc2_error_t>(error);
}

hwc2_error_t hwc2_compat_display_set_power_mode(hwc2_compat_display_t* display,
                                        int mode)
{
    display->device->self->setPowerMode(mode);
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_display_set_vsync_enabled(hwc2_compat_display_t* display,
                                           int enabled)
{
    display->device->self->setVsyncEnabled(enabled);
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_display_validate(hwc2_compat_display_t* display,
                                 uint32_t* outNumTypes,
                                 uint32_t* outNumRequests)
{
    //const int expectedPresentTime = 0;
    //hal::Error error = display->self->validate(expectedPresentTime, 0, outNumTypes, outNumRequests);
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_buffer(hwc2_compat_layer_t* layer,
                                          uint32_t slot,
                                          struct ANativeWindowBuffer* buffer,
                                          const int32_t acquireFenceFd)
{
    int32_t error = HWC2_ERROR_NONE;
    HardwareBufferDescription description = {
        .width = static_cast<int32_t>(buffer->width),
        .height = static_cast<int32_t>(buffer->height),
        .layers = static_cast<int32_t>(1),
        .format = static_cast<PixelFormat>(buffer->format),
        .usage = static_cast<BufferUsage>(buffer->usage),
        .stride = static_cast<int32_t>(buffer->stride),
    };
    HardwareBuffer out = {
        .description = description,
        .handle = dupToAidl(buffer->handle),
    };

    layer->display->device->self->setBuffer(static_cast<int64_t>(layer->display->id), out, acquireFenceFd, &error);
    return static_cast<hwc2_error_t>(error);
}

hwc2_error_t hwc2_compat_layer_set_blend_mode(hwc2_compat_layer_t* layer, int mode)
{
    //hal::Error error = layer->self->setBlendMode(
    //    static_cast<hal::BlendMode>(mode));
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_color(hwc2_compat_layer_t* layer,
                                    hwc_color_t color)
{
    //hal::Error error = layer->self->setColor({
    //    color.r, color.g, color.b, color.a});

    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_composition_type(hwc2_compat_layer_t* layer,
                                            int type)
{
    //hal::Error error = layer->self->setCompositionType(
    //    static_cast<hal::Composition>(type));
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_dataspace(hwc2_compat_layer_t* layer,
                                        android_dataspace_t dataspace)
{
    //hal::Error error = layer->self->setDataspace(
    //    static_cast<hal::Dataspace>(dataspace));
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_display_frame(hwc2_compat_layer_t* layer,
                                            int32_t left, int32_t top,
                                            int32_t right, int32_t bottom)
{
    //android::Rect r = {left, top, right, bottom};

    //hal::Error error = layer->self->setDisplayFrame(r);
    return HWC2_ERROR_NONE;
}
hwc2_error_t hwc2_compat_layer_set_plane_alpha(hwc2_compat_layer_t* layer,
                                        float alpha)
{
    //hal::Error error = layer->self->setPlaneAlpha(alpha);
    return HWC2_ERROR_NONE;
}
hwc2_error_t hwc2_compat_layer_set_sideband_stream(hwc2_compat_layer_t* layer,
                                            const native_handle_t* stream)
{
    //hal::Error error = layer->self->setSidebandStream(stream);
    return HWC2_ERROR_NONE;
}
hwc2_error_t hwc2_compat_layer_set_source_crop(hwc2_compat_layer_t* layer,
                                        float left, float top,
                                        float right, float bottom)
{
    //android::FloatRect r = {left, top, right, bottom};

    //hal::Error error = layer->self->setSourceCrop(r);
    return HWC2_ERROR_NONE;
}
hwc2_error_t hwc2_compat_layer_set_transform(hwc2_compat_layer_t* layer,
                                        int transform)
{
    //hal::Error error = layer->self->setTransform(
    //    static_cast<Hwc2::Transform>(transform));
    return HWC2_ERROR_NONE;
}

hwc2_error_t hwc2_compat_layer_set_visible_region(hwc2_compat_layer_t* layer,
                                            int32_t left, int32_t top,
                                            int32_t right, int32_t bottom)
{
    //android::Rect r = {left, top, right, bottom};

    //hal::Error error = layer->self->setVisibleRegion(android::Region(r));
    return HWC2_ERROR_NONE;
}

int32_t hwc2_compat_out_fences_get_fence(hwc2_compat_out_fences_t* fences,
                                         hwc2_compat_layer_t* layer)
{
    auto iter = fences->fences.find(layer->display->id);

    if(iter != fences->fences.end()) {
        return iter->second->dup();
    } else {
        return -1;
    }
}

void hwc2_compat_out_fences_destroy(hwc2_compat_out_fences_t* fences)
{
    delete fences;
}
