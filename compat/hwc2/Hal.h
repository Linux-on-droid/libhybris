/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android/hardware/graphics/common/1.1/types.h>
#include <android/hardware/graphics/composer/2.4/IComposer.h>
#include <android/hardware/graphics/composer/2.4/IComposerClient.h>

#if ANDROID_VERSION_MAJOR >= 13
#include <aidl/android/hardware/graphics/composer3/Composition.h>
#include <aidl/android/hardware/graphics/composer3/DisplayCapability.h>
#endif

#define ERROR_HAS_CHANGES 5

namespace android {
namespace hardware::graphics::composer::hal {

namespace types = android::hardware::graphics::common;
namespace V2_1 = android::hardware::graphics::composer::V2_1;
namespace V2_2 = android::hardware::graphics::composer::V2_2;
namespace V2_3 = android::hardware::graphics::composer::V2_3;
namespace V2_4 = android::hardware::graphics::composer::V2_4;

using types::V1_0::ColorTransform;
using types::V1_0::Transform;
using types::V1_1::RenderIntent;
using types::V1_2::ColorMode;
using types::V1_2::Dataspace;
using types::V1_2::Hdr;
using types::V1_2::PixelFormat;

using V2_1::Error;
using V2_4::IComposer;
using V2_4::IComposerCallback;
using V2_4::IComposerClient;
using V2_4::VsyncPeriodChangeTimeline;
using V2_4::VsyncPeriodNanos;

using Attribute = IComposerClient::Attribute;
using BlendMode = IComposerClient::BlendMode;
using Connection = IComposerCallback::Connection;
using ContentType = IComposerClient::ContentType;
using ClientTargetProperty = IComposerClient::ClientTargetProperty;
using DisplayRequest = IComposerClient::DisplayRequest;
using DisplayType = IComposerClient::DisplayType;
using HWConfigId = V2_1::Config;
using HWDisplayId = V2_1::Display;
using HWError = V2_1::Error;
using HWLayerId = V2_1::Layer;
using LayerGenericMetadataKey = IComposerClient::LayerGenericMetadataKey;
using LayerRequest = IComposerClient::LayerRequest;
using PerFrameMetadata = IComposerClient::PerFrameMetadata;
using PerFrameMetadataKey = IComposerClient::PerFrameMetadataKey;
using PerFrameMetadataBlob = IComposerClient::PerFrameMetadataBlob;
using PowerMode = IComposerClient::PowerMode;
using Vsync = IComposerClient::Vsync;
using VsyncPeriodChangeConstraints = IComposerClient::VsyncPeriodChangeConstraints;

#if ANDROID_VERSION_MAJOR >= 13
namespace V3_0 = ::aidl::android::hardware::graphics::composer3;
using V3_0::Capability;
using V3_0::Color;
using V3_0::Composition;
using V3_0::DisplayCapability;
#else
using Capability = IComposer::Capability;
using Color = IComposerClient::Color;
using Composition = IComposerClient::Composition;
using DisplayCapability = IComposerClient::DisplayCapability;
#endif

} // namespace hardware::graphics::composer::hal

inline bool hasChangesError(hardware::graphics::composer::hal::Error error) {
    return ERROR_HAS_CHANGES == static_cast<int32_t>(error);
}

inline std::string to_string(hardware::graphics::composer::hal::Attribute attribute) {
    switch (attribute) {
        case hardware::graphics::composer::hal::Attribute::INVALID:
            return "Invalid";
        case hardware::graphics::composer::hal::Attribute::WIDTH:
            return "Width";
        case hardware::graphics::composer::hal::Attribute::HEIGHT:
            return "Height";
        case hardware::graphics::composer::hal::Attribute::VSYNC_PERIOD:
            return "VsyncPeriod";
        case hardware::graphics::composer::hal::Attribute::DPI_X:
            return "DpiX";
        case hardware::graphics::composer::hal::Attribute::DPI_Y:
            return "DpiY";
        default:
            return "Unknown";
    }
}

inline std::string to_string(
        hardware::graphics::composer::hal::Composition composition) {
    switch (composition) {
        case hardware::graphics::composer::hal::Composition::INVALID:
            return "Invalid";
        case hardware::graphics::composer::hal::Composition::CLIENT:
            return "Client";
        case hardware::graphics::composer::hal::Composition::DEVICE:
            return "Device";
        case hardware::graphics::composer::hal::Composition::SOLID_COLOR:
            return "SolidColor";
        case hardware::graphics::composer::hal::Composition::CURSOR:
            return "Cursor";
        case hardware::graphics::composer::hal::Composition::SIDEBAND:
            return "Sideband";
#if ANDROID_VERSION_MAJOR >= 13
        case hardware::graphics::composer::hal::Composition::DISPLAY_DECORATION:
            return "DisplayDecoration";
#endif
        default:
            return "Unknown";
    }
}

inline std::string to_string(
        hardware::graphics::composer::hal::DisplayCapability displayCapability) {
    switch (displayCapability) {
        case hardware::graphics::composer::hal::DisplayCapability::INVALID:
            return "Invalid";
        case hardware::graphics::composer::hal::DisplayCapability::
                SKIP_CLIENT_COLOR_TRANSFORM:
            return "SkipColorTransform";
        case hardware::graphics::composer::hal::DisplayCapability::DOZE:
            return "Doze";
        case hardware::graphics::composer::hal::DisplayCapability::BRIGHTNESS:
            return "Brightness";
        case hardware::graphics::composer::hal::DisplayCapability::PROTECTED_CONTENTS:
            return "ProtectedContents";
        case hardware::graphics::composer::hal::DisplayCapability::AUTO_LOW_LATENCY_MODE:
            return "AutoLowLatencyMode";
#if ANDROID_VERSION_MAJOR >= 13
        case hardware::graphics::composer::hal::DisplayCapability::SUSPEND:
            return "Suspend";
        case hardware::graphics::composer::hal::DisplayCapability::DISPLAY_IDLE_TIMER:
            return "DisplayIdleTimer";
#endif
        default:
            return "Unknown";
    }
}

inline std::string to_string(hardware::graphics::composer::hal::V2_4::Error error) {
    // 5 is reserved for historical reason, during validation 5 means has changes.
    if (ERROR_HAS_CHANGES == static_cast<int32_t>(error)) {
        return "HasChanges";
    }
    switch (error) {
        case hardware::graphics::composer::hal::V2_4::Error::NONE:
            return "None";
        case hardware::graphics::composer::hal::V2_4::Error::BAD_CONFIG:
            return "BadConfig";
        case hardware::graphics::composer::hal::V2_4::Error::BAD_DISPLAY:
            return "BadDisplay";
        case hardware::graphics::composer::hal::V2_4::Error::BAD_LAYER:
            return "BadLayer";
        case hardware::graphics::composer::hal::V2_4::Error::BAD_PARAMETER:
            return "BadParameter";
        case hardware::graphics::composer::hal::V2_4::Error::NO_RESOURCES:
            return "NoResources";
        case hardware::graphics::composer::hal::V2_4::Error::NOT_VALIDATED:
            return "NotValidated";
        case hardware::graphics::composer::hal::V2_4::Error::UNSUPPORTED:
            return "Unsupported";
        case hardware::graphics::composer::hal::V2_4::Error::SEAMLESS_NOT_ALLOWED:
            return "SeamlessNotAllowed";
        case hardware::graphics::composer::hal::V2_4::Error::SEAMLESS_NOT_POSSIBLE:
            return "SeamlessNotPossible";
        default:
            return "Unknown";
    }
}

inline std::string to_string(hardware::graphics::composer::hal::Error error) {
    return to_string(static_cast<hardware::graphics::composer::hal::V2_4::Error>(error));
}

inline std::string to_string(hardware::graphics::composer::hal::PowerMode mode) {
    switch (mode) {
        case hardware::graphics::composer::hal::PowerMode::OFF:
            return "Off";
        case hardware::graphics::composer::hal::PowerMode::DOZE:
            return "Doze";
        case hardware::graphics::composer::hal::PowerMode::ON:
            return "On";
        case hardware::graphics::composer::hal::PowerMode::DOZE_SUSPEND:
            return "DozeSuspend";
        case hardware::graphics::composer::hal::PowerMode::ON_SUSPEND:
            return "OnSuspend";
        default:
            return "Unknown";
    }
}

inline std::string to_string(hardware::graphics::composer::hal::Vsync vsync) {
    switch (vsync) {
        case hardware::graphics::composer::hal::Vsync::ENABLE:
            return "Enable";
        case hardware::graphics::composer::hal::Vsync::DISABLE:
            return "Disable";
        default:
            return "Unknown";
    }
}

} // namespace android
