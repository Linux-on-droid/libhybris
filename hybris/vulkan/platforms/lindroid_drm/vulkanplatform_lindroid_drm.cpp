/*
 * Copyright (c) 2026 The Lindroid Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-config.h>
#include <assert.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ws.h"

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define VK_USE_PLATFORM_WAYLAND_KHR 1
extern "C" {
#include <vulkanplatformcommon.h>
};
#include <vulkanhybris.h>

extern "C" {
#include <wayland-client.h>
#include <wayland-egl.h>
}

#include <vulkan/vulkan.h>

#include <hybris/gralloc/gralloc.h>
#include <hybris/common/binding.h>
#include "logging.h"
#include "wayland_window.h"

struct LindroidDisplay {
    wl_display *wl_dpy;
    WaylandNativeWindow *window;
    struct wl_egl_window *egl_window;
};

static bool init_done = false;

static std::map<VkSurfaceKHR, struct LindroidDisplay *> _surface_window_map;

static int lindroid_has_mapping(VkSurfaceKHR surface)
{
    return (_surface_window_map.find(surface) != _surface_window_map.end());
}

static void lindroid_push_mapping(VkSurfaceKHR surface, struct LindroidDisplay *ldpy)
{
    assert(!lindroid_has_mapping(surface));

    _surface_window_map[surface] = ldpy;
}

static struct LindroidDisplay *lindroid_pop_mapping(VkSurfaceKHR surface)
{
    std::map<VkSurfaceKHR, struct LindroidDisplay *>::iterator it;
    it = _surface_window_map.find(surface);

    assert(it != _surface_window_map.end());

    struct LindroidDisplay *result = it->second;
    _surface_window_map.erase(it);
    return result;
}

static VkResult (*_vkCreateAndroidSurfaceKHR)(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) = NULL;
static PFN_vkVoidFunction (*_vkDestroySurfaceKHR)(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) = NULL;
static VkResult (*_vkEnumerateInstanceExtensionProperties)(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties) = NULL;
static VkResult (*_vkCreateInstance)(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) = NULL;
static PFN_vkVoidFunction (*_vkGetInstanceProcAddr)(VkInstance instance, const char *pName) = NULL;

extern "C" void lindroid_drmws_init_module(struct ws_vulkan_interface *vulkan_iface)
{
    if (init_done) {
        return;
    }
    hybris_gralloc_initialize(0);
    vulkanplatformcommon_init(vulkan_iface);
    init_done = true;
}

static void freeLindroidDisplay(LindroidDisplay *ldpy)
{
    delete ldpy;
}

static VkResult lindroid_drmws_vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    VkResult res;

    if (_vkEnumerateInstanceExtensionProperties == NULL) {
        _vkEnumerateInstanceExtensionProperties = (VkResult (*)(const char*, uint32_t*, VkExtensionProperties*))
            (*_vkGetInstanceProcAddr)(NULL, "vkEnumerateInstanceExtensionProperties");
    }

    res = (*_vkEnumerateInstanceExtensionProperties)(pLayerName, pPropertyCount, pProperties);
    if (res == VK_SUCCESS && *pPropertyCount > 0 && pProperties != NULL) {
        uint32_t i;
        for (i = 0; i < *pPropertyCount; i++) {
            if (strcmp(pProperties[i].extensionName, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME) == 0) {
                strncpy(pProperties[i].extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
            }
        }
    }
    return res;
}

static VkResult lindroid_drmws_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    VkInstanceCreateInfo createInfo = *pCreateInfo;
    VkResult result;
    char **enabledExtensions = (char **)malloc(pCreateInfo->enabledExtensionCount * sizeof(char *));
    uint32_t i;

    if (_vkCreateInstance == NULL) {
        _vkCreateInstance = (VkResult (*)(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *))
            (*_vkGetInstanceProcAddr)(NULL, "vkCreateInstance");
    }

    for (i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        enabledExtensions[i] = (char *)malloc(VK_MAX_EXTENSION_NAME_SIZE * sizeof(char));
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
            strncpy(enabledExtensions[i], VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        } else {
            strncpy(enabledExtensions[i], pCreateInfo->ppEnabledExtensionNames[i], VK_MAX_EXTENSION_NAME_SIZE);
        }
    }
    createInfo.ppEnabledExtensionNames = enabledExtensions;

    result = (*_vkCreateInstance)(&createInfo, pAllocator, pInstance);

    for (i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        free(enabledExtensions[i]);
    }
    free(enabledExtensions);

    return result;
}

static VkResult lindroid_drmws_vkCreateWaylandSurfaceKHR(VkInstance instance,
        const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSurfaceKHR* pSurface)
{
    VkAndroidSurfaceCreateInfoKHR createInfo;
    VkResult result;
    LindroidDisplay *ldpy = new LindroidDisplay;
    WaylandNativeWindow *win;

    HYBRIS_TRACE_BEGIN("hybris-vulkan", "vkCreateWaylandSurfaceKHR", "");

    if (_vkCreateAndroidSurfaceKHR == NULL) {
        _vkCreateAndroidSurfaceKHR = (VkResult (*)(VkInstance, const VkAndroidSurfaceCreateInfoKHR *, const VkAllocationCallbacks *, VkSurfaceKHR *))
            (*_vkGetInstanceProcAddr)(instance, "vkCreateAndroidSurfaceKHR");
    }

    ldpy->wl_dpy = pCreateInfo->display;

    ldpy->egl_window = wl_egl_window_create(pCreateInfo->surface, 1, 1);

    HYBRIS_TRACE_BEGIN("native-vulkan", "vkCreateWaylandSurfaceKHR", "");

    // NULL android_wlegl selects the zwp_linux_dmabuf_v1
    win = new WaylandNativeWindow(ldpy->egl_window, ldpy->wl_dpy, NULL);
    win->common.incRef(&win->common);
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.window = win;

    result = (*_vkCreateAndroidSurfaceKHR)(instance, &createInfo, pAllocator, pSurface);

    HYBRIS_TRACE_END("native-vulkan", "vkCreateWaylandSurfaceKHR", "");

    if (result == VK_SUCCESS) {
        ldpy->window = win;
        lindroid_push_mapping(*pSurface, ldpy);
    } else {
        HYBRIS_ERROR("vkCreateAndroidSurfaceKHR failed");
        win->common.decRef(&win->common);
        freeLindroidDisplay(ldpy);
    }

    HYBRIS_TRACE_END("hybris-vulkan", "vkCreateWaylandSurfaceKHR", "");
    return result;
}

static VkBool32 lindroid_drmws_vkGetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display)
{
    return VK_TRUE;
}

static void lindroid_drmws_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
{
    if (lindroid_has_mapping(surface)) {
        LindroidDisplay *ldpy = lindroid_pop_mapping(surface);
        WaylandNativeWindow *window = ldpy->window;

        if (_vkDestroySurfaceKHR == NULL) {
            _vkDestroySurfaceKHR = (PFN_vkVoidFunction (*)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *))
                (*_vkGetInstanceProcAddr)(instance, "vkDestroySurfaceKHR");
        }

        window->destroyWlEGLWindow();
        window->common.decRef(&window->common);
        _vkDestroySurfaceKHR(instance, surface, pAllocator);
        freeLindroidDisplay(ldpy);
    }
}

extern "C" void lindroid_drmws_vkSetInstanceProcAddrFunc(PFN_vkVoidFunction addr)
{
    if (_vkGetInstanceProcAddr == NULL)
        _vkGetInstanceProcAddr = (PFN_vkVoidFunction (*)(VkInstance, const char*))addr;
}

struct ws_module ws_module_info = {
    lindroid_drmws_init_module,

    lindroid_drmws_vkEnumerateInstanceExtensionProperties,
    lindroid_drmws_vkCreateInstance,
    lindroid_drmws_vkCreateWaylandSurfaceKHR,
    lindroid_drmws_vkGetPhysicalDeviceWaylandPresentationSupportKHR,
    lindroid_drmws_vkDestroySurfaceKHR,
    lindroid_drmws_vkSetInstanceProcAddrFunc,
};

// vim:ts=4:sw=4:noexpandtab
