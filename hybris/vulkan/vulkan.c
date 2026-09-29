/*
 * Copyright (c) 2022 Jolla Ltd.
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
 *
 */

/* For RTLD_DEFAULT */
#define _GNU_SOURCE

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define VK_USE_PLATFORM_WAYLAND_KHR 1

#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <xf86drm.h>
#include <cutils/native_handle.h>
#include "evdi_drm.h"

#include <hybris/common/binding.h>
#include <hybris/common/floating_point_abi.h>
#include "config.h"
#include "logging.h"
#include "ws.h"

static void *vulkan_handle = NULL;
/*
 * This generates a function that when first called overwrites it's plt entry with new address.
 * Subsequent calls jump directly at the target function in the android library.
 * This means effectively 0 call overhead after the first call.
 */
#if defined(__aarch64__)

static void _init_androidvulkan(void);

struct hybris_vk_stub {
    void *target;
    const char *name;
};

void *hybris_vulkan_resolve(struct hybris_vk_stub *stub);

void *hybris_vulkan_resolve(struct hybris_vk_stub *stub)
{
    if (!vulkan_handle)
        _init_androidvulkan();

    void *target = android_dlsym(vulkan_handle, stub->name);
    __atomic_store_n(&stub->target, target, __ATOMIC_RELEASE);
    return target;
}

__asm__(
"   .text\n"
"   .align 4\n"
"   .type hybris_vulkan_trampoline, %function\n"
"hybris_vulkan_trampoline:\n"
"   hint #34\n"
"   stp x29, x30, [sp, #-224]!\n"
"   mov x29, sp\n"
"   stp x0, x1, [sp, #16]\n"
"   stp x2, x3, [sp, #32]\n"
"   stp x4, x5, [sp, #48]\n"
"   stp x6, x7, [sp, #64]\n"
"   str x8,     [sp, #80]\n"
"   stp q0, q1, [sp, #96]\n"
"   stp q2, q3, [sp, #128]\n"
"   stp q4, q5, [sp, #160]\n"
"   stp q6, q7, [sp, #192]\n"
"   mov x0, x16\n"                     // x16 = &stub
"   bl hybris_vulkan_resolve\n"
"   mov x16, x0\n"
"   ldp q6, q7, [sp, #192]\n"
"   ldp q4, q5, [sp, #160]\n"
"   ldp q2, q3, [sp, #128]\n"
"   ldp q0, q1, [sp, #96]\n"
"   ldr x8,     [sp, #80]\n"
"   ldp x6, x7, [sp, #64]\n"
"   ldp x4, x5, [sp, #48]\n"
"   ldp x2, x3, [sp, #32]\n"
"   ldp x0, x1, [sp, #16]\n"
"   ldp x29, x30, [sp], #224\n"
"   br x16\n"
"   .size hybris_vulkan_trampoline, .-hybris_vulkan_trampoline\n"
);

#define VULKAN_IDLOAD(sym) \
__asm__( \
"   .pushsection .data, \"aw\"\n" \
"   .align 3\n" \
".Lstub_" #sym ":\n" \
"   .quad 0\n" \
"   .quad .Lname_" #sym "\n" \
".Lname_" #sym ": .asciz \"" #sym "\"\n" \
"   .popsection\n" \
"   .text\n" \
"   .align 2\n" \
"   .globl " #sym "\n" \
"   .type " #sym ", %function\n" \
#sym ":\n" \
"   hint #34\n"  \
"   adrp x16, .Lstub_" #sym "\n" \
"   add  x16, x16, :lo12:.Lstub_" #sym "\n" \
"   ldr  x17, [x16]\n" \
"   cbnz x17, 8f\n" \
"   b    hybris_vulkan_trampoline\n" \
"8: br   x17\n" \
"   .size " #sym ", .-" #sym "\n" \
);

#else

#define VULKAN_IDLOAD(sym) \
 __asm__ (".type " #sym ", %gnu_indirect_function"); \
typeof(sym) * sym ## _dispatch (void) __asm__ (#sym);\
typeof(sym) * sym ## _dispatch (void) \
{ \
    if (!vulkan_handle) _init_androidvulkan(); \
    return (void *) android_dlsym(vulkan_handle, #sym); \
}

#endif

static void _init_androidvulkan(void)
{
    vulkan_handle = (void *) android_dlopen(getenv("LIBVULKAN") ? getenv("LIBVULKAN") : "libvulkan.so", RTLD_LAZY);
}

static inline void hybris_vulkan_initialize()
{
    _init_androidvulkan();
}

static void * _android_vulkan_dlsym(const char *symbol)
{
    if (vulkan_handle == NULL)
        _init_androidvulkan();

    return android_dlsym(vulkan_handle, symbol);
}

struct ws_vulkan_interface hybris_vulkan_interface = {
    _android_vulkan_dlsym,
};

static PFN_vkVoidFunction (*_vkGetInstanceProcAddr)(VkInstance instance, const char* pName) = NULL;

/* Use IDLOAD approach also for float functions, since vulkan uses the aapcs-vfp calling convention even on android */

VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    if (_vkGetInstanceProcAddr == NULL) {
        HYBRIS_DLSYSM(vulkan, &_vkGetInstanceProcAddr, "vkGetInstanceProcAddr");
    }
    ws_vkSetInstanceProcAddrFunc((PFN_vkVoidFunction)_vkGetInstanceProcAddr);

    return ws_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

VULKAN_IDLOAD(vkDestroyInstance);
VULKAN_IDLOAD(vkEnumeratePhysicalDevices);
VULKAN_IDLOAD(vkGetPhysicalDeviceFeatures);
VULKAN_IDLOAD(vkGetPhysicalDeviceFormatProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceImageFormatProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceQueueFamilyProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceMemoryProperties);

VkResult vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    if (_vkGetInstanceProcAddr == NULL) {
        HYBRIS_DLSYSM(vulkan, &_vkGetInstanceProcAddr, "vkGetInstanceProcAddr");
    }
    ws_vkSetInstanceProcAddrFunc((PFN_vkVoidFunction)_vkGetInstanceProcAddr);

    return ws_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

static VkResult (*_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) = NULL;

VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    VkResult result;

    if (_vkGetPhysicalDeviceSurfaceCapabilitiesKHR == NULL) {
        HYBRIS_DLSYSM(vulkan, &_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    }

    result = (*_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(physicalDevice, surface, pSurfaceCapabilities);
    if (result == VK_SUCCESS && pSurfaceCapabilities != NULL) {
        pSurfaceCapabilities->currentExtent.width = 0xFFFFFFFF;
        pSurfaceCapabilities->currentExtent.height = 0xFFFFFFFF;
    }
    return result;
}

#ifdef WANT_WAYLAND
VkResult vkCreateWaylandSurfaceKHR(VkInstance instance,
        const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSurfaceKHR* pSurface)
{
    return ws_vkCreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}

VkBool32 vkGetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display)
{
    return ws_vkGetPhysicalDeviceWaylandPresentationSupportKHR(physicalDevice, queueFamilyIndex, display);
}

void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
{
    ws_vkDestroySurfaceKHR(instance, surface, pAllocator);
}
#else
VULKAN_IDLOAD(vkDestroySurfaceKHR);
#endif

#ifndef VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME
#define VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME "VK_EXT_physical_device_drm"
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT ((VkStructureType)1000353000)
typedef struct VkPhysicalDeviceDrmPropertiesEXT {
    VkStructureType sType;
    void*           pNext;
    VkBool32        hasPrimary;
    VkBool32        hasRender;
    int64_t         primaryMajor;
    int64_t         primaryMinor;
    int64_t         renderMajor;
    int64_t         renderMinor;
} VkPhysicalDeviceDrmPropertiesEXT;
#endif

struct hybris_drm_node {
    bool present;
    int64_t major;
    int64_t minor;
};

static struct { struct hybris_drm_node primary, render; bool probed; } _evdi_nodes;

static void _probe_node(const char *env, const char *path, struct hybris_drm_node *out)
{
    const char *override = getenv(env);
    struct stat st;

    if (override != NULL)
        path = override;

    if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
        return;

    out->present = true;
    out->major = (int64_t)major(st.st_rdev);
    out->minor = (int64_t)minor(st.st_rdev);
}

static void _probe_evdi_nodes(void)
{
    if (_evdi_nodes.probed)
        return;
    _evdi_nodes.probed = true;

    _probe_node("LINDROID_EVDI_CARD",
                "/dev/dri/by-path/platform-evdi-lindroid.0-card",
                &_evdi_nodes.primary);
    _probe_node("LINDROID_EVDI_RENDER",
                "/dev/dri/by-path/platform-evdi-lindroid.0-render",
                &_evdi_nodes.render);
}

static void _answer_drm_properties(void *pNext)
{
    VkBaseOutStructure *s;

    for (s = (VkBaseOutStructure *)pNext; s != NULL; s = s->pNext) {
        if (s->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT)
            continue;

        VkPhysicalDeviceDrmPropertiesEXT *drm = (VkPhysicalDeviceDrmPropertiesEXT *)s;

        _probe_evdi_nodes();

        drm->hasPrimary   = _evdi_nodes.primary.present ? VK_TRUE : VK_FALSE;
        drm->primaryMajor = _evdi_nodes.primary.major;
        drm->primaryMinor = _evdi_nodes.primary.minor;
        drm->hasRender    = _evdi_nodes.render.present ? VK_TRUE : VK_FALSE;
        drm->renderMajor  = _evdi_nodes.render.major;
        drm->renderMinor  = _evdi_nodes.render.minor;
    }
}

#define HYBRIS_VK_DMA_BUF_EXTENSION_NAME "VK_EXT_external_memory_dma_buf"
#define HYBRIS_VK_AHB_EXTENSION_NAME "VK_ANDROID_external_memory_android_hardware_buffer"
#define HYBRIS_VK_MODIFIER_EXTENSION_NAME "VK_EXT_image_drm_format_modifier"

#define HYBRIS_VK_MODIFIER 0ULL // DRM_FORMAT_MOD_LINEAR

#ifndef VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
#define VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT 0x00000200
#endif

static const char *const _synthetic_device_extensions[] = {
    VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
    HYBRIS_VK_DMA_BUF_EXTENSION_NAME,
    HYBRIS_VK_MODIFIER_EXTENSION_NAME,
};
#define HYBRIS_SYNTHETIC_EXT_COUNT \
    (sizeof(_synthetic_device_extensions) / sizeof(_synthetic_device_extensions[0]))

static bool _is_synthetic_extension(const char *name)
{
    for (size_t i = 0; i < HYBRIS_SYNTHETIC_EXT_COUNT; i++)
        if (strcmp(name, _synthetic_device_extensions[i]) == 0)
            return true;
    return false;
}

static int _evdi_fd(void)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int fd = -1;

    pthread_mutex_lock(&lock);
    if (fd < 0) {
        const char *path = getenv("LINDROID_EVDI_CARD");
        if (path == NULL)
            path = "/dev/dri/by-path/platform-evdi-lindroid.0-card";
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            drm_auth_t auth = { .magic = 0 };
            if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth) != -1 || errno != EACCES)
                (void)ioctl(fd, DRM_IOCTL_DROP_MASTER, NULL);
        }
    }
    pthread_mutex_unlock(&lock);

    return fd;
}

struct hybris_image_info {
    VkImage image;
    uint32_t width, height, stride_hint;
    VkFormat format;
    bool modifier_image;
    struct hybris_image_info *next;
};

static struct hybris_image_info *_image_list = NULL;
static pthread_mutex_t _image_lock = PTHREAD_MUTEX_INITIALIZER;

static void _image_remember(VkImage image, const VkImageCreateInfo *info)
{
    struct hybris_image_info *e = calloc(1, sizeof(*e));
    if (e == NULL)
        return;

    e->image = image;
    e->width = info->extent.width;
    e->height = info->extent.height;
    e->format = info->format;

    pthread_mutex_lock(&_image_lock);
    e->next = _image_list;
    _image_list = e;
    pthread_mutex_unlock(&_image_lock);
}

static bool _image_lookup(VkImage image, struct hybris_image_info *out)
{
    bool found = false;

    pthread_mutex_lock(&_image_lock);
    for (struct hybris_image_info *e = _image_list; e != NULL; e = e->next) {
        if (e->image == image) {
            *out = *e;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&_image_lock);

    return found;
}

static void _image_forget(VkImage image)
{
    pthread_mutex_lock(&_image_lock);
    for (struct hybris_image_info **p = &_image_list; *p != NULL; p = &(*p)->next) {
        if ((*p)->image == image) {
            struct hybris_image_info *dead = *p;
            *p = dead->next;
            free(dead);
            break;
        }
    }
    pthread_mutex_unlock(&_image_lock);
}

struct AHardwareBuffer;
typedef struct {
    uint32_t width, height, layers, format;
    uint64_t usage;
    uint32_t stride, rfu0;
    uint64_t rfu1;
} hybris_AHardwareBuffer_Desc;

#define HYBRIS_AHB_CREATE_FROM_HANDLE_REGISTER 2

static int (*_AHardwareBuffer_createFromHandle)(const hybris_AHardwareBuffer_Desc *,
                                                const native_handle_t *, int32_t,
                                                struct AHardwareBuffer **) = NULL;

static bool _init_ahardwarebuffer(void)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static bool tried = false;

    pthread_mutex_lock(&lock);
    if (!tried) {
        tried = true;
        void *lnw = android_dlopen("libnativewindow.so", RTLD_LAZY);
        if (lnw != NULL)
            _AHardwareBuffer_createFromHandle = android_dlsym(lnw, "AHardwareBuffer_createFromHandle");
    }
    pthread_mutex_unlock(&lock);

    return _AHardwareBuffer_createFromHandle != NULL;
}

#define HYBRIS_GRALLOC_USAGE_HW_TEXTURE  0x00000100ULL
#define HYBRIS_GRALLOC_USAGE_HW_RENDER   0x00000200ULL
#define HYBRIS_GRALLOC_USAGE_HW_COMPOSER 0x00000800ULL

static uint32_t _hal_format_from_vk(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8_UNORM:            return 3;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:     return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:     return 22;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:return 43;
    default:                                return 1;
    }
}

static uint32_t _bytes_per_pixel_from_vk(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8_UNORM:            return 3;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:     return 2;
    case VK_FORMAT_R16G16B16A16_SFLOAT:     return 8;
    default:                                return 4;
    }
}

static struct AHardwareBuffer *_ahb_from_lindroid_fd(int fd, const struct hybris_image_info *geom)
{
    int id = -1;

    if (fd < 0 || geom == NULL)
        return NULL;

    if (pread(fd, &id, sizeof(id), 0) != (ssize_t)sizeof(id)) {
        HYBRIS_ERROR("lindroid-drm: dmabuf fd %d is not readable metadata", fd);
        return NULL;
    }

    if (id <= 0) {
        HYBRIS_ERROR("lindroid-drm: dmabuf fd %d carries no evdi buffer id (%d)", fd, id);
        return NULL;
    }

    int drm = _evdi_fd();
    if (drm < 0) {
        HYBRIS_ERROR("lindroid-drm: no evdi node to resolve buffer %d", id);
        return NULL;
    }

    struct drm_evdi_gbm_get_buff get;
    memset(&get, 0, sizeof(get));
    get.id = id;
    get.native_handle = malloc(max_native_handle_size);
    if (get.native_handle == NULL)
        return NULL;

    if (ioctl(drm, DRM_IOCTL_EVDI_GBM_GET_BUFF, &get) < 0) {
        HYBRIS_ERROR("lindroid-drm: GBM_GET_BUFF(%d) failed: %s", id, strerror(errno));
        free(get.native_handle);
        return NULL;
    }

    if (!_init_ahardwarebuffer()) {
        HYBRIS_ERROR("lindroid-drm: AHardwareBuffer_createFromHandle unavailable");
        free(get.native_handle);
        return NULL;
    }

    uint32_t bpp = _bytes_per_pixel_from_vk(geom->format);
    uint32_t stride = geom->width;
    struct stat st;

    if (fstat(fd, &st) == 0 && geom->height > 0 && bpp > 0) {
        uint32_t from_size = (uint32_t)(st.st_size / geom->height / bpp);
        if (from_size > 0)
            stride = from_size;
    }

    hybris_AHardwareBuffer_Desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = geom->width;
    desc.height = geom->height;
    desc.layers = 1;
    desc.format = _hal_format_from_vk(geom->format);
    desc.usage = HYBRIS_GRALLOC_USAGE_HW_TEXTURE | HYBRIS_GRALLOC_USAGE_HW_RENDER |
                 HYBRIS_GRALLOC_USAGE_HW_COMPOSER;
    desc.stride = stride;

    struct AHardwareBuffer *ahb = NULL;
    int rc = (*_AHardwareBuffer_createFromHandle)(&desc, (const native_handle_t *)get.native_handle,
                                                  HYBRIS_AHB_CREATE_FROM_HANDLE_REGISTER, &ahb);

    if (rc != 0 || ahb == NULL) {
        HYBRIS_ERROR("lindroid-drm: AHardwareBuffer_createFromHandle(%d) failed: %d", id, rc);
        free(get.native_handle);
        return NULL;
    }

    return ahb;
}

static VkResult (*_vkCreateImage)(VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage *) = NULL;

VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
    if (_vkCreateImage == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkCreateImage, "vkCreateImage");

    if (pCreateInfo == NULL)
        return (*_vkCreateImage)(device, pCreateInfo, pAllocator, pImage);

    bool is_modifier = (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
    bool saw_external = false;

    VkImageCreateInfo info = *pCreateInfo;
    VkExternalMemoryImageCreateInfo ext_copy;
    VkBaseOutStructure *head = NULL, **tail = &head;

    for (const VkBaseInStructure *c = pCreateInfo->pNext; c != NULL; c = c->pNext) {
        switch ((int)c->sType) {
        case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT:
        case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT:
            is_modifier = true;
            continue;
        case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO: {
            const VkExternalMemoryImageCreateInfo *e = (const VkExternalMemoryImageCreateInfo *)c;
            ext_copy = *e;
            ext_copy.pNext = NULL;
            if (ext_copy.handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
                ext_copy.handleTypes &= ~(VkExternalMemoryHandleTypeFlags)VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
                ext_copy.handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
                saw_external = true;
            }
            *tail = (VkBaseOutStructure *)&ext_copy;
            tail = &(*tail)->pNext;
            continue;
        }
        default:
            *tail = (VkBaseOutStructure *)(void *)(uintptr_t)c;
            tail = NULL;
            break;
        }
        if (tail == NULL)
            break;
    }
    if (tail != NULL)
        *tail = NULL;

    if (is_modifier) {
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.pNext = head;
        (void)saw_external;
    }

    VkResult res = (*_vkCreateImage)(device, is_modifier ? &info : pCreateInfo, pAllocator, pImage);
    if (res == VK_SUCCESS && pImage != NULL) {
        _image_remember(*pImage, pCreateInfo);
        if (is_modifier) {
            struct hybris_image_info probe;
            if (_image_lookup(*pImage, &probe)) {
                pthread_mutex_lock(&_image_lock);
                for (struct hybris_image_info *e = _image_list; e != NULL; e = e->next)
                    if (e->image == *pImage)
                        e->modifier_image = true;
                pthread_mutex_unlock(&_image_lock);
            }
        }
    }

    return res;
}

static void (*_vkGetPhysicalDeviceFormatProperties2)(VkPhysicalDevice, VkFormat, VkFormatProperties2 *) = NULL;

void vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                          VkFormatProperties2 *pFormatProperties)
{
    if (_vkGetPhysicalDeviceFormatProperties2 == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkGetPhysicalDeviceFormatProperties2, "vkGetPhysicalDeviceFormatProperties2");

    if (pFormatProperties == NULL) {
        (*_vkGetPhysicalDeviceFormatProperties2)(physicalDevice, format, pFormatProperties);
        return;
    }

    VkBaseOutStructure *prev = NULL, *list_node = NULL;

    for (VkBaseOutStructure *c = (VkBaseOutStructure *)pFormatProperties->pNext, *p = NULL;
         c != NULL; p = c, c = c->pNext) {
        if (c->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT) {
            list_node = c;
            prev = p;
            break;
        }
    }

    if (list_node != NULL) {
        if (prev != NULL)
            prev->pNext = list_node->pNext;
        else
            pFormatProperties->pNext = list_node->pNext;
    }

    (*_vkGetPhysicalDeviceFormatProperties2)(physicalDevice, format, pFormatProperties);

    if (list_node != NULL) {
        if (prev != NULL)
            prev->pNext = list_node;
        else
            pFormatProperties->pNext = list_node;
    }

    if (list_node != NULL) {
        VkFormatFeatureFlags features = pFormatProperties->formatProperties.optimalTilingFeatures;
        VkDrmFormatModifierPropertiesListEXT *list = (VkDrmFormatModifierPropertiesListEXT *)list_node;

        if (features == 0) {
            list->drmFormatModifierCount = 0;
        } else if (list->pDrmFormatModifierProperties == NULL) {
            list->drmFormatModifierCount = 1;
        } else if (list->drmFormatModifierCount >= 1) {
            list->pDrmFormatModifierProperties[0].drmFormatModifier = HYBRIS_VK_MODIFIER;
            list->pDrmFormatModifierProperties[0].drmFormatModifierPlaneCount = 1;
            list->pDrmFormatModifierProperties[0].drmFormatModifierTilingFeatures = features;
            list->drmFormatModifierCount = 1;
        }
    }
}

void vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format,
                                             VkFormatProperties2 *pFormatProperties)
{
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

static VkResult (*_vkGetPhysicalDeviceImageFormatProperties2)(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2 *, VkImageFormatProperties2 *) = NULL;

VkResult vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                                   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                                   VkImageFormatProperties2 *pImageFormatProperties)
{
    if (_vkGetPhysicalDeviceImageFormatProperties2 == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkGetPhysicalDeviceImageFormatProperties2, "vkGetPhysicalDeviceImageFormatProperties2");

    if (pImageFormatInfo == NULL)
        return (*_vkGetPhysicalDeviceImageFormatProperties2)(physicalDevice, pImageFormatInfo, pImageFormatProperties);

    VkPhysicalDeviceImageFormatInfo2 info = *pImageFormatInfo;
    VkPhysicalDeviceExternalImageFormatInfo ext_copy;
    VkBaseOutStructure *head = NULL, **tail = &head;
    bool is_modifier = (pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
    bool wants_dma_buf = false;

    for (const VkBaseInStructure *c = pImageFormatInfo->pNext; c != NULL; c = c->pNext) {
        if (c->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT) {
            is_modifier = true;
            continue;
        }
        if (c->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO) {
            const VkPhysicalDeviceExternalImageFormatInfo *e = (const VkPhysicalDeviceExternalImageFormatInfo *)c;
            ext_copy = *e;
            ext_copy.pNext = NULL;
            if (ext_copy.handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
                ext_copy.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
                wants_dma_buf = true;
            }
            *tail = (VkBaseOutStructure *)&ext_copy;
            tail = &(*tail)->pNext;
            continue;
        }
        *tail = (VkBaseOutStructure *)(void *)(uintptr_t)c;
        tail = NULL;
        break;
    }
    if (tail != NULL)
        *tail = NULL;

    if (is_modifier) {
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.pNext = head;
    }

    VkResult res = (*_vkGetPhysicalDeviceImageFormatProperties2)(physicalDevice,
                       is_modifier ? &info : pImageFormatInfo, pImageFormatProperties);

    if (res == VK_SUCCESS && wants_dma_buf && pImageFormatProperties != NULL) {
        for (VkBaseOutStructure *c = (VkBaseOutStructure *)pImageFormatProperties->pNext; c != NULL; c = c->pNext) {
            if (c->sType != VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES)
                continue;
            VkExternalImageFormatProperties *p = (VkExternalImageFormatProperties *)c;
            p->externalMemoryProperties.externalMemoryFeatures =
                VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;
            p->externalMemoryProperties.compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            p->externalMemoryProperties.exportFromImportedHandleTypes = 0;
        }
    }

    return res;
}

VkResult vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice,
                                                      const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                                      VkImageFormatProperties2 *pImageFormatProperties)
{
    return vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

VkResult vkGetImageDrmFormatModifierPropertiesEXT(VkDevice device, VkImage image,
                                                  VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
    (void)device;

    if (pProperties == NULL)
        return VK_ERROR_INITIALIZATION_FAILED;

    struct hybris_image_info info;
    if (!_image_lookup(image, &info))
        return VK_ERROR_INITIALIZATION_FAILED;

    pProperties->drmFormatModifier = HYBRIS_VK_MODIFIER;

    return VK_SUCCESS;
}

static void (*_vkDestroyImage)(VkDevice, VkImage, const VkAllocationCallbacks *) = NULL;

void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator)
{
    if (_vkDestroyImage == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkDestroyImage, "vkDestroyImage");

    _image_forget(image);
    (*_vkDestroyImage)(device, image, pAllocator);
}

static VkResult (*_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory *) = NULL;

VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                          const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory)
{
    if (_vkAllocateMemory == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkAllocateMemory, "vkAllocateMemory");

    if (pAllocateInfo == NULL)
        return (*_vkAllocateMemory)(device, pAllocateInfo, pAllocator, pMemory);

    const VkImportMemoryFdInfoKHR *fd_info = NULL;
    VkImage dedicated_image = VK_NULL_HANDLE;

    for (const VkBaseInStructure *c = pAllocateInfo->pNext; c != NULL; c = c->pNext) {
        if (c->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR) {
            const VkImportMemoryFdInfoKHR *f = (const VkImportMemoryFdInfoKHR *)c;
            if (f->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
                fd_info = f;
        } else if (c->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) {
            dedicated_image = ((const VkMemoryDedicatedAllocateInfo *)c)->image;
        }
    }

    if (fd_info == NULL)
        return (*_vkAllocateMemory)(device, pAllocateInfo, pAllocator, pMemory);

    struct hybris_image_info geom;
    if (dedicated_image == VK_NULL_HANDLE || !_image_lookup(dedicated_image, &geom)) {
        HYBRIS_ERROR("lindroid-drm: dma-buf import without a known dedicated image");
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }

    struct AHardwareBuffer *ahb = _ahb_from_lindroid_fd(fd_info->fd, &geom);
    if (ahb == NULL)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;

    VkImportAndroidHardwareBufferInfoANDROID ahb_info;
    memset(&ahb_info, 0, sizeof(ahb_info));
    ahb_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    ahb_info.buffer = (struct AHardwareBuffer *)ahb;

    VkMemoryAllocateInfo alloc = *pAllocateInfo;
    VkBaseOutStructure *head = NULL, **tail = &head;

    for (const VkBaseInStructure *c = pAllocateInfo->pNext; c != NULL; c = c->pNext) {
        if (c == (const VkBaseInStructure *)fd_info)
            continue;
        *tail = (VkBaseOutStructure *)(void *)(uintptr_t)c;
        tail = &(*tail)->pNext;
    }
    ahb_info.pNext = NULL;
    *tail = (VkBaseOutStructure *)&ahb_info;
    alloc.pNext = head ? (const void *)head : (const void *)&ahb_info;

    VkResult res = (*_vkAllocateMemory)(device, &alloc, pAllocator, pMemory);
    if (res != VK_SUCCESS)
        HYBRIS_ERROR("lindroid-drm: AHardwareBuffer import failed: %d", res);

    return res;
}

VkResult vkGetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType,
                                    int fd, VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
    if (handleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT || pMemoryFdProperties == NULL)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;

    (void)device;
    (void)fd;
    pMemoryFdProperties->memoryTypeBits = ~0u;

    return VK_SUCCESS;
}

static VkResult (*_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *) = NULL;

VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                              uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    if (_vkEnumerateDeviceExtensionProperties == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");

    if (pLayerName != NULL)
        return (*_vkEnumerateDeviceExtensionProperties)(physicalDevice, pLayerName, pPropertyCount, pProperties);

    if (pProperties == NULL) {
        VkResult res = (*_vkEnumerateDeviceExtensionProperties)(physicalDevice, pLayerName, pPropertyCount, NULL);
        if (res == VK_SUCCESS)
            *pPropertyCount += HYBRIS_SYNTHETIC_EXT_COUNT;
        return res;
    }

    if (*pPropertyCount <= HYBRIS_SYNTHETIC_EXT_COUNT)
        return VK_INCOMPLETE;

    uint32_t driver_count = *pPropertyCount - HYBRIS_SYNTHETIC_EXT_COUNT;
    VkResult res = (*_vkEnumerateDeviceExtensionProperties)(physicalDevice, pLayerName, &driver_count, pProperties);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)
        return res;

    for (size_t i = 0; i < HYBRIS_SYNTHETIC_EXT_COUNT; i++) {
        VkExtensionProperties *slot = &pProperties[driver_count + i];
        strncpy(slot->extensionName, _synthetic_device_extensions[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
        slot->extensionName[VK_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
        slot->specVersion = 1;
    }
    *pPropertyCount = driver_count + HYBRIS_SYNTHETIC_EXT_COUNT;

    return res;
}

static void (*_vkGetPhysicalDeviceProperties2)(VkPhysicalDevice, VkPhysicalDeviceProperties2 *) = NULL;

void vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties)
{
    if (_vkGetPhysicalDeviceProperties2 == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkGetPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");

    (*_vkGetPhysicalDeviceProperties2)(physicalDevice, pProperties);
    if (pProperties != NULL)
        _answer_drm_properties(pProperties->pNext);
}

void vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties)
{
    vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

static VkResult (*_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *) = NULL;

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    if (_vkCreateDevice == NULL)
        HYBRIS_DLSYSM(vulkan, &_vkCreateDevice, "vkCreateDevice");

    if (pCreateInfo == NULL || pCreateInfo->enabledExtensionCount == 0)
        return (*_vkCreateDevice)(physicalDevice, pCreateInfo, pAllocator, pDevice);

    VkDeviceCreateInfo createInfo = *pCreateInfo;
    const char **names = malloc((pCreateInfo->enabledExtensionCount + 1) * sizeof(*names));
    if (names == NULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    uint32_t count = 0;
    bool wants_dma_buf = false, has_ahb = false;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        const char *name = pCreateInfo->ppEnabledExtensionNames[i];

        if (strcmp(name, HYBRIS_VK_DMA_BUF_EXTENSION_NAME) == 0)
            wants_dma_buf = true;
        if (strcmp(name, HYBRIS_VK_AHB_EXTENSION_NAME) == 0)
            has_ahb = true;
        if (_is_synthetic_extension(name))
            continue;

        names[count++] = name;
    }

    if (wants_dma_buf && !has_ahb)
        names[count++] = HYBRIS_VK_AHB_EXTENSION_NAME;

    createInfo.enabledExtensionCount = count;
    createInfo.ppEnabledExtensionNames = names;

    VkResult res = (*_vkCreateDevice)(physicalDevice, &createInfo, pAllocator, pDevice);
    free(names);
    return res;
}

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (_vkGetInstanceProcAddr == NULL) {
        HYBRIS_DLSYSM(vulkan, &_vkGetInstanceProcAddr, "vkGetInstanceProcAddr");
    }

    if (!strcmp(pName, "vkEnumerateInstanceExtensionProperties")) {
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    } else if (!strcmp(pName, "vkCreateInstance")) {
        return (PFN_vkVoidFunction)vkCreateInstance;
    } else if (!strcmp(pName, "vkGetInstanceProcAddr")) {
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    } else if (!strcmp(pName, "vkEnumerateDeviceExtensionProperties")) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceProperties2")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceProperties2KHR")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2KHR;
    } else if (!strcmp(pName, "vkCreateDevice")) {
        return (PFN_vkVoidFunction)vkCreateDevice;
    } else if (!strcmp(pName, "vkAllocateMemory")) {
        return (PFN_vkVoidFunction)vkAllocateMemory;
    } else if (!strcmp(pName, "vkCreateImage")) {
        return (PFN_vkVoidFunction)vkCreateImage;
    } else if (!strcmp(pName, "vkDestroyImage")) {
        return (PFN_vkVoidFunction)vkDestroyImage;
    } else if (!strcmp(pName, "vkGetMemoryFdPropertiesKHR")) {
        return (PFN_vkVoidFunction)vkGetMemoryFdPropertiesKHR;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties2")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties2;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties2KHR;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties2;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2KHR")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties2KHR;
    } else if (!strcmp(pName, "vkGetImageDrmFormatModifierPropertiesEXT")) {
        return (PFN_vkVoidFunction)vkGetImageDrmFormatModifierPropertiesEXT;
#ifdef WANT_WAYLAND
    } else if (!strcmp(pName, "vkCreateWaylandSurfaceKHR")) {
        return (PFN_vkVoidFunction)vkCreateWaylandSurfaceKHR;
    } else if (!strcmp(pName, "vkGetPhysicalDeviceWaylandPresentationSupportKHR")) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceWaylandPresentationSupportKHR;
    } else if (!strcmp(pName, "vkDestroySurfaceKHR")) {
        return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
#endif
    }

    return (*_vkGetInstanceProcAddr)(instance, pName);
}

VULKAN_IDLOAD(vkGetDeviceProcAddr);
VULKAN_IDLOAD(vkDestroyDevice);
VULKAN_IDLOAD(vkEnumerateInstanceLayerProperties);
VULKAN_IDLOAD(vkEnumerateDeviceLayerProperties);
VULKAN_IDLOAD(vkGetDeviceQueue);
VULKAN_IDLOAD(vkQueueSubmit);
VULKAN_IDLOAD(vkQueueWaitIdle);
VULKAN_IDLOAD(vkDeviceWaitIdle);
VULKAN_IDLOAD(vkFreeMemory);
VULKAN_IDLOAD(vkMapMemory);
VULKAN_IDLOAD(vkUnmapMemory);
VULKAN_IDLOAD(vkFlushMappedMemoryRanges);
VULKAN_IDLOAD(vkInvalidateMappedMemoryRanges);
VULKAN_IDLOAD(vkGetDeviceMemoryCommitment);
VULKAN_IDLOAD(vkBindBufferMemory);
VULKAN_IDLOAD(vkBindImageMemory);
VULKAN_IDLOAD(vkGetBufferMemoryRequirements);
VULKAN_IDLOAD(vkGetImageMemoryRequirements);
VULKAN_IDLOAD(vkGetImageSparseMemoryRequirements);
VULKAN_IDLOAD(vkGetPhysicalDeviceSparseImageFormatProperties);
VULKAN_IDLOAD(vkQueueBindSparse);
VULKAN_IDLOAD(vkCreateFence);
VULKAN_IDLOAD(vkDestroyFence);
VULKAN_IDLOAD(vkResetFences);
VULKAN_IDLOAD(vkGetFenceStatus);
VULKAN_IDLOAD(vkWaitForFences);
VULKAN_IDLOAD(vkCreateSemaphore);
VULKAN_IDLOAD(vkDestroySemaphore);
VULKAN_IDLOAD(vkCreateEvent);
VULKAN_IDLOAD(vkDestroyEvent);
VULKAN_IDLOAD(vkGetEventStatus);
VULKAN_IDLOAD(vkSetEvent);
VULKAN_IDLOAD(vkResetEvent);
VULKAN_IDLOAD(vkCreateQueryPool);
VULKAN_IDLOAD(vkDestroyQueryPool);
VULKAN_IDLOAD(vkGetQueryPoolResults);
VULKAN_IDLOAD(vkCreateBuffer);
VULKAN_IDLOAD(vkDestroyBuffer);
VULKAN_IDLOAD(vkCreateBufferView);
VULKAN_IDLOAD(vkDestroyBufferView);
VULKAN_IDLOAD(vkGetImageSubresourceLayout);
VULKAN_IDLOAD(vkCreateImageView);
VULKAN_IDLOAD(vkDestroyImageView);
VULKAN_IDLOAD(vkCreateShaderModule);
VULKAN_IDLOAD(vkDestroyShaderModule);
VULKAN_IDLOAD(vkCreatePipelineCache);
VULKAN_IDLOAD(vkDestroyPipelineCache);
VULKAN_IDLOAD(vkGetPipelineCacheData);
VULKAN_IDLOAD(vkMergePipelineCaches);
VULKAN_IDLOAD(vkCreateGraphicsPipelines);
VULKAN_IDLOAD(vkCreateComputePipelines);
VULKAN_IDLOAD(vkDestroyPipeline);
VULKAN_IDLOAD(vkCreatePipelineLayout);
VULKAN_IDLOAD(vkDestroyPipelineLayout);
VULKAN_IDLOAD(vkCreateSampler);
VULKAN_IDLOAD(vkDestroySampler);
VULKAN_IDLOAD(vkCreateDescriptorSetLayout);
VULKAN_IDLOAD(vkDestroyDescriptorSetLayout);
VULKAN_IDLOAD(vkCreateDescriptorPool);
VULKAN_IDLOAD(vkDestroyDescriptorPool);
VULKAN_IDLOAD(vkResetDescriptorPool);
VULKAN_IDLOAD(vkAllocateDescriptorSets);
VULKAN_IDLOAD(vkFreeDescriptorSets);
VULKAN_IDLOAD(vkUpdateDescriptorSets);
VULKAN_IDLOAD(vkCreateFramebuffer);
VULKAN_IDLOAD(vkDestroyFramebuffer);
VULKAN_IDLOAD(vkCreateRenderPass);
VULKAN_IDLOAD(vkDestroyRenderPass);
VULKAN_IDLOAD(vkGetRenderAreaGranularity);
VULKAN_IDLOAD(vkCreateCommandPool);
VULKAN_IDLOAD(vkDestroyCommandPool);
VULKAN_IDLOAD(vkResetCommandPool);
VULKAN_IDLOAD(vkAllocateCommandBuffers);
VULKAN_IDLOAD(vkFreeCommandBuffers);
VULKAN_IDLOAD(vkBeginCommandBuffer);
VULKAN_IDLOAD(vkEndCommandBuffer);
VULKAN_IDLOAD(vkResetCommandBuffer);
VULKAN_IDLOAD(vkCmdBindPipeline);
VULKAN_IDLOAD(vkCmdSetViewport);
VULKAN_IDLOAD(vkCmdSetScissor);
VULKAN_IDLOAD(vkCmdSetLineWidth);
VULKAN_IDLOAD(vkCmdSetDepthBias);
VULKAN_IDLOAD(vkCmdSetBlendConstants);
VULKAN_IDLOAD(vkCmdSetDepthBounds);
VULKAN_IDLOAD(vkCmdSetStencilCompareMask);
VULKAN_IDLOAD(vkCmdSetStencilWriteMask);
VULKAN_IDLOAD(vkCmdSetStencilReference);
VULKAN_IDLOAD(vkCmdBindDescriptorSets);
VULKAN_IDLOAD(vkCmdBindIndexBuffer);
VULKAN_IDLOAD(vkCmdBindVertexBuffers);
VULKAN_IDLOAD(vkCmdDraw);
VULKAN_IDLOAD(vkCmdDrawIndexed);
VULKAN_IDLOAD(vkCmdDrawIndirect);
VULKAN_IDLOAD(vkCmdDrawIndexedIndirect);
VULKAN_IDLOAD(vkCmdDispatch);
VULKAN_IDLOAD(vkCmdDispatchIndirect);
VULKAN_IDLOAD(vkCmdCopyBuffer);
VULKAN_IDLOAD(vkCmdCopyImage);
VULKAN_IDLOAD(vkCmdBlitImage);
VULKAN_IDLOAD(vkCmdCopyBufferToImage);
VULKAN_IDLOAD(vkCmdCopyImageToBuffer);
VULKAN_IDLOAD(vkCmdUpdateBuffer);
VULKAN_IDLOAD(vkCmdFillBuffer);
VULKAN_IDLOAD(vkCmdClearColorImage);
VULKAN_IDLOAD(vkCmdClearDepthStencilImage);
VULKAN_IDLOAD(vkCmdClearAttachments);
VULKAN_IDLOAD(vkCmdResolveImage);
VULKAN_IDLOAD(vkCmdSetEvent);
VULKAN_IDLOAD(vkCmdResetEvent);
VULKAN_IDLOAD(vkCmdWaitEvents);
VULKAN_IDLOAD(vkCmdPipelineBarrier);
VULKAN_IDLOAD(vkCmdBeginQuery);
VULKAN_IDLOAD(vkCmdEndQuery);
VULKAN_IDLOAD(vkCmdResetQueryPool);
VULKAN_IDLOAD(vkCmdWriteTimestamp);
VULKAN_IDLOAD(vkCmdCopyQueryPoolResults);
VULKAN_IDLOAD(vkCmdPushConstants);
VULKAN_IDLOAD(vkCmdBeginRenderPass);
VULKAN_IDLOAD(vkCmdNextSubpass);
VULKAN_IDLOAD(vkCmdEndRenderPass);
VULKAN_IDLOAD(vkCmdExecuteCommands);
VULKAN_IDLOAD(vkEnumerateInstanceVersion);
VULKAN_IDLOAD(vkBindBufferMemory2);
VULKAN_IDLOAD(vkBindImageMemory2);
VULKAN_IDLOAD(vkGetDeviceGroupPeerMemoryFeatures);
VULKAN_IDLOAD(vkCmdSetDeviceMask);
VULKAN_IDLOAD(vkCmdDispatchBase);
VULKAN_IDLOAD(vkEnumeratePhysicalDeviceGroups);
VULKAN_IDLOAD(vkGetImageMemoryRequirements2);
VULKAN_IDLOAD(vkGetBufferMemoryRequirements2);
VULKAN_IDLOAD(vkGetImageSparseMemoryRequirements2);
VULKAN_IDLOAD(vkGetPhysicalDeviceFeatures2);
VULKAN_IDLOAD(vkGetPhysicalDeviceQueueFamilyProperties2);
VULKAN_IDLOAD(vkGetPhysicalDeviceMemoryProperties2);
VULKAN_IDLOAD(vkGetPhysicalDeviceSparseImageFormatProperties2);
VULKAN_IDLOAD(vkTrimCommandPool);
VULKAN_IDLOAD(vkGetDeviceQueue2);
VULKAN_IDLOAD(vkCreateSamplerYcbcrConversion);
VULKAN_IDLOAD(vkDestroySamplerYcbcrConversion);
VULKAN_IDLOAD(vkCreateDescriptorUpdateTemplate);
VULKAN_IDLOAD(vkDestroyDescriptorUpdateTemplate);
VULKAN_IDLOAD(vkUpdateDescriptorSetWithTemplate);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalBufferProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalFenceProperties);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalSemaphoreProperties);
VULKAN_IDLOAD(vkGetDescriptorSetLayoutSupport);
VULKAN_IDLOAD(vkCmdDrawIndirectCount);
VULKAN_IDLOAD(vkCmdDrawIndexedIndirectCount);
VULKAN_IDLOAD(vkCreateRenderPass2);
VULKAN_IDLOAD(vkCmdBeginRenderPass2);
VULKAN_IDLOAD(vkCmdNextSubpass2);
VULKAN_IDLOAD(vkCmdEndRenderPass2);
VULKAN_IDLOAD(vkResetQueryPool);
VULKAN_IDLOAD(vkGetSemaphoreCounterValue);
VULKAN_IDLOAD(vkWaitSemaphores);
VULKAN_IDLOAD(vkSignalSemaphore);
VULKAN_IDLOAD(vkGetBufferDeviceAddress);
VULKAN_IDLOAD(vkGetBufferOpaqueCaptureAddress);
VULKAN_IDLOAD(vkGetDeviceMemoryOpaqueCaptureAddress);
#if VK_HEADER_VERSION >= 204
VULKAN_IDLOAD(vkGetPhysicalDeviceToolProperties);
VULKAN_IDLOAD(vkCreatePrivateDataSlot);
VULKAN_IDLOAD(vkDestroyPrivateDataSlot);
VULKAN_IDLOAD(vkSetPrivateData);
VULKAN_IDLOAD(vkGetPrivateData);
VULKAN_IDLOAD(vkCmdSetEvent2);
VULKAN_IDLOAD(vkCmdResetEvent2);
VULKAN_IDLOAD(vkCmdWaitEvents2);
VULKAN_IDLOAD(vkCmdPipelineBarrier2);
VULKAN_IDLOAD(vkCmdWriteTimestamp2);
VULKAN_IDLOAD(vkQueueSubmit2);
VULKAN_IDLOAD(vkCmdCopyBuffer2);
VULKAN_IDLOAD(vkCmdCopyImage2);
VULKAN_IDLOAD(vkCmdCopyBufferToImage2);
VULKAN_IDLOAD(vkCmdCopyImageToBuffer2);
VULKAN_IDLOAD(vkCmdBlitImage2);
VULKAN_IDLOAD(vkCmdResolveImage2);
VULKAN_IDLOAD(vkCmdBeginRendering);
VULKAN_IDLOAD(vkCmdEndRendering);
VULKAN_IDLOAD(vkCmdSetCullMode);
VULKAN_IDLOAD(vkCmdSetFrontFace);
VULKAN_IDLOAD(vkCmdSetPrimitiveTopology);
VULKAN_IDLOAD(vkCmdSetViewportWithCount);
VULKAN_IDLOAD(vkCmdSetScissorWithCount);
VULKAN_IDLOAD(vkCmdBindVertexBuffers2);
VULKAN_IDLOAD(vkCmdSetDepthTestEnable);
VULKAN_IDLOAD(vkCmdSetDepthWriteEnable);
VULKAN_IDLOAD(vkCmdSetDepthCompareOp);
VULKAN_IDLOAD(vkCmdSetDepthBoundsTestEnable);
VULKAN_IDLOAD(vkCmdSetStencilTestEnable);
VULKAN_IDLOAD(vkCmdSetStencilOp);
VULKAN_IDLOAD(vkCmdSetRasterizerDiscardEnable);
VULKAN_IDLOAD(vkCmdSetDepthBiasEnable);
VULKAN_IDLOAD(vkCmdSetPrimitiveRestartEnable);
VULKAN_IDLOAD(vkGetDeviceBufferMemoryRequirements);
VULKAN_IDLOAD(vkGetDeviceImageMemoryRequirements);
VULKAN_IDLOAD(vkGetDeviceImageSparseMemoryRequirements);
#endif
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfaceSupportKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfaceFormatsKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfacePresentModesKHR);
VULKAN_IDLOAD(vkCreateSwapchainKHR);
VULKAN_IDLOAD(vkDestroySwapchainKHR);
VULKAN_IDLOAD(vkGetSwapchainImagesKHR);
VULKAN_IDLOAD(vkAcquireNextImageKHR);
VULKAN_IDLOAD(vkQueuePresentKHR);
VULKAN_IDLOAD(vkGetDeviceGroupPresentCapabilitiesKHR);
VULKAN_IDLOAD(vkGetDeviceGroupSurfacePresentModesKHR);
VULKAN_IDLOAD(vkGetPhysicalDevicePresentRectanglesKHR);
VULKAN_IDLOAD(vkAcquireNextImage2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceDisplayPropertiesKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
VULKAN_IDLOAD(vkGetDisplayPlaneSupportedDisplaysKHR);
VULKAN_IDLOAD(vkGetDisplayModePropertiesKHR);
VULKAN_IDLOAD(vkCreateDisplayModeKHR);
VULKAN_IDLOAD(vkGetDisplayPlaneCapabilitiesKHR);
VULKAN_IDLOAD(vkCreateDisplayPlaneSurfaceKHR);
VULKAN_IDLOAD(vkCreateSharedSwapchainsKHR);
#if VK_HEADER_VERSION >= 238
VULKAN_IDLOAD(vkGetPhysicalDeviceVideoCapabilitiesKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceVideoFormatPropertiesKHR);
VULKAN_IDLOAD(vkCreateVideoSessionKHR);
VULKAN_IDLOAD(vkDestroyVideoSessionKHR);
VULKAN_IDLOAD(vkGetVideoSessionMemoryRequirementsKHR);
VULKAN_IDLOAD(vkBindVideoSessionMemoryKHR);
VULKAN_IDLOAD(vkCreateVideoSessionParametersKHR);
VULKAN_IDLOAD(vkUpdateVideoSessionParametersKHR);
VULKAN_IDLOAD(vkDestroyVideoSessionParametersKHR);
VULKAN_IDLOAD(vkCmdBeginVideoCodingKHR);
VULKAN_IDLOAD(vkCmdEndVideoCodingKHR);
VULKAN_IDLOAD(vkCmdControlVideoCodingKHR);
VULKAN_IDLOAD(vkCmdDecodeVideoKHR);
#endif
#if VK_HEADER_VERSION >= 197
VULKAN_IDLOAD(vkCmdBeginRenderingKHR);
VULKAN_IDLOAD(vkCmdEndRenderingKHR);
#endif
VULKAN_IDLOAD(vkGetPhysicalDeviceFeatures2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceMemoryProperties2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceSparseImageFormatProperties2KHR);
VULKAN_IDLOAD(vkGetDeviceGroupPeerMemoryFeaturesKHR);
VULKAN_IDLOAD(vkCmdSetDeviceMaskKHR);
VULKAN_IDLOAD(vkCmdDispatchBaseKHR);
VULKAN_IDLOAD(vkTrimCommandPoolKHR);
VULKAN_IDLOAD(vkEnumeratePhysicalDeviceGroupsKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalBufferPropertiesKHR);
VULKAN_IDLOAD(vkGetMemoryFdKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
VULKAN_IDLOAD(vkImportSemaphoreFdKHR);
VULKAN_IDLOAD(vkGetSemaphoreFdKHR);
VULKAN_IDLOAD(vkCmdPushDescriptorSetKHR);
VULKAN_IDLOAD(vkCmdPushDescriptorSetWithTemplateKHR);
VULKAN_IDLOAD(vkCreateDescriptorUpdateTemplateKHR);
VULKAN_IDLOAD(vkDestroyDescriptorUpdateTemplateKHR);
VULKAN_IDLOAD(vkUpdateDescriptorSetWithTemplateKHR);
VULKAN_IDLOAD(vkCreateRenderPass2KHR);
VULKAN_IDLOAD(vkCmdBeginRenderPass2KHR);
VULKAN_IDLOAD(vkCmdNextSubpass2KHR);
VULKAN_IDLOAD(vkCmdEndRenderPass2KHR);
VULKAN_IDLOAD(vkGetSwapchainStatusKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalFencePropertiesKHR);
VULKAN_IDLOAD(vkImportFenceFdKHR);
VULKAN_IDLOAD(vkGetFenceFdKHR);
VULKAN_IDLOAD(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR);
VULKAN_IDLOAD(vkAcquireProfilingLockKHR);
VULKAN_IDLOAD(vkReleaseProfilingLockKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfaceFormats2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceDisplayProperties2KHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceDisplayPlaneProperties2KHR);
VULKAN_IDLOAD(vkGetDisplayModeProperties2KHR);
VULKAN_IDLOAD(vkGetDisplayPlaneCapabilities2KHR);
VULKAN_IDLOAD(vkGetImageMemoryRequirements2KHR);
VULKAN_IDLOAD(vkGetBufferMemoryRequirements2KHR);
VULKAN_IDLOAD(vkGetImageSparseMemoryRequirements2KHR);
VULKAN_IDLOAD(vkCreateSamplerYcbcrConversionKHR);
VULKAN_IDLOAD(vkDestroySamplerYcbcrConversionKHR);
VULKAN_IDLOAD(vkBindBufferMemory2KHR);
VULKAN_IDLOAD(vkBindImageMemory2KHR);
VULKAN_IDLOAD(vkGetDescriptorSetLayoutSupportKHR);
VULKAN_IDLOAD(vkCmdDrawIndirectCountKHR);
VULKAN_IDLOAD(vkCmdDrawIndexedIndirectCountKHR);
VULKAN_IDLOAD(vkGetSemaphoreCounterValueKHR);
VULKAN_IDLOAD(vkWaitSemaphoresKHR);
VULKAN_IDLOAD(vkSignalSemaphoreKHR);
VULKAN_IDLOAD(vkGetPhysicalDeviceFragmentShadingRatesKHR);
VULKAN_IDLOAD(vkCmdSetFragmentShadingRateKHR);
#if VK_HEADER_VERSION >= 276
VULKAN_IDLOAD(vkCmdSetRenderingAttachmentLocationsKHR);
VULKAN_IDLOAD(vkCmdSetRenderingInputAttachmentIndicesKHR);
#endif
#if VK_HEADER_VERSION >= 185
VULKAN_IDLOAD(vkWaitForPresentKHR);
#endif
VULKAN_IDLOAD(vkGetBufferDeviceAddressKHR);
VULKAN_IDLOAD(vkGetBufferOpaqueCaptureAddressKHR);
VULKAN_IDLOAD(vkGetDeviceMemoryOpaqueCaptureAddressKHR);
VULKAN_IDLOAD(vkCreateDeferredOperationKHR);
VULKAN_IDLOAD(vkDestroyDeferredOperationKHR);
VULKAN_IDLOAD(vkGetDeferredOperationMaxConcurrencyKHR);
VULKAN_IDLOAD(vkGetDeferredOperationResultKHR);
VULKAN_IDLOAD(vkDeferredOperationJoinKHR);
VULKAN_IDLOAD(vkGetPipelineExecutablePropertiesKHR);
VULKAN_IDLOAD(vkGetPipelineExecutableStatisticsKHR);
VULKAN_IDLOAD(vkGetPipelineExecutableInternalRepresentationsKHR);
#if VK_HEADER_VERSION >= 244
VULKAN_IDLOAD(vkMapMemory2KHR);
VULKAN_IDLOAD(vkUnmapMemory2KHR);
#endif
#if VK_HEADER_VERSION >= 274
VULKAN_IDLOAD(vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR);
VULKAN_IDLOAD(vkGetEncodedVideoSessionParametersKHR);
VULKAN_IDLOAD(vkCmdEncodeVideoKHR);
#endif
VULKAN_IDLOAD(vkCmdSetEvent2KHR);
VULKAN_IDLOAD(vkCmdResetEvent2KHR);
VULKAN_IDLOAD(vkCmdWaitEvents2KHR);
VULKAN_IDLOAD(vkCmdPipelineBarrier2KHR);
VULKAN_IDLOAD(vkCmdWriteTimestamp2KHR);
VULKAN_IDLOAD(vkQueueSubmit2KHR);
VULKAN_IDLOAD(vkCmdWriteBufferMarker2AMD);
VULKAN_IDLOAD(vkGetQueueCheckpointData2NV);
VULKAN_IDLOAD(vkCmdCopyBuffer2KHR);
VULKAN_IDLOAD(vkCmdCopyImage2KHR);
VULKAN_IDLOAD(vkCmdCopyBufferToImage2KHR);
VULKAN_IDLOAD(vkCmdCopyImageToBuffer2KHR);
VULKAN_IDLOAD(vkCmdBlitImage2KHR);
VULKAN_IDLOAD(vkCmdResolveImage2KHR);
#if VK_HEADER_VERSION >= 213
VULKAN_IDLOAD(vkCmdTraceRaysIndirect2KHR);
#endif
#if VK_HEADER_VERSION >= 195
VULKAN_IDLOAD(vkGetDeviceBufferMemoryRequirementsKHR);
VULKAN_IDLOAD(vkGetDeviceImageMemoryRequirementsKHR);
VULKAN_IDLOAD(vkGetDeviceImageSparseMemoryRequirementsKHR);
#endif
#if VK_HEADER_VERSION >= 260
VULKAN_IDLOAD(vkCmdBindIndexBuffer2KHR);
VULKAN_IDLOAD(vkGetRenderingAreaGranularityKHR);
VULKAN_IDLOAD(vkGetDeviceImageSubresourceLayoutKHR);
VULKAN_IDLOAD(vkGetImageSubresourceLayout2KHR);
#endif
#if VK_HEADER_VERSION >= 255
VULKAN_IDLOAD(vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR);
#endif
#if VK_HEADER_VERSION >= 276
VULKAN_IDLOAD(vkCmdSetLineStippleKHR);
#endif
#if VK_HEADER_VERSION >= 273
VULKAN_IDLOAD(vkGetPhysicalDeviceCalibrateableTimeDomainsKHR);
VULKAN_IDLOAD(vkGetCalibratedTimestampsKHR);
#endif
#if VK_HEADER_VERSION >= 274
VULKAN_IDLOAD(vkCmdBindDescriptorSets2KHR);
VULKAN_IDLOAD(vkCmdPushConstants2KHR);
VULKAN_IDLOAD(vkCmdPushDescriptorSet2KHR);
VULKAN_IDLOAD(vkCmdPushDescriptorSetWithTemplate2KHR);
VULKAN_IDLOAD(vkCmdSetDescriptorBufferOffsets2EXT);
VULKAN_IDLOAD(vkCmdBindDescriptorBufferEmbeddedSamplers2EXT);
#endif
VULKAN_IDLOAD(vkCreateDebugReportCallbackEXT);
VULKAN_IDLOAD(vkDestroyDebugReportCallbackEXT);
VULKAN_IDLOAD(vkDebugReportMessageEXT);
VULKAN_IDLOAD(vkDebugMarkerSetObjectTagEXT);
VULKAN_IDLOAD(vkDebugMarkerSetObjectNameEXT);
VULKAN_IDLOAD(vkCmdDebugMarkerBeginEXT);
VULKAN_IDLOAD(vkCmdDebugMarkerEndEXT);
VULKAN_IDLOAD(vkCmdDebugMarkerInsertEXT);
VULKAN_IDLOAD(vkCmdBindTransformFeedbackBuffersEXT);
VULKAN_IDLOAD(vkCmdBeginTransformFeedbackEXT);
VULKAN_IDLOAD(vkCmdEndTransformFeedbackEXT);
VULKAN_IDLOAD(vkCmdBeginQueryIndexedEXT);
VULKAN_IDLOAD(vkCmdEndQueryIndexedEXT);
VULKAN_IDLOAD(vkCmdDrawIndirectByteCountEXT);
VULKAN_IDLOAD(vkCreateCuModuleNVX);
VULKAN_IDLOAD(vkCreateCuFunctionNVX);
VULKAN_IDLOAD(vkDestroyCuModuleNVX);
VULKAN_IDLOAD(vkDestroyCuFunctionNVX);
VULKAN_IDLOAD(vkCmdCuLaunchKernelNVX);
VULKAN_IDLOAD(vkGetImageViewHandleNVX);
VULKAN_IDLOAD(vkGetImageViewAddressNVX);
VULKAN_IDLOAD(vkCmdDrawIndirectCountAMD);
VULKAN_IDLOAD(vkCmdDrawIndexedIndirectCountAMD);
VULKAN_IDLOAD(vkGetShaderInfoAMD);
VULKAN_IDLOAD(vkGetPhysicalDeviceExternalImageFormatPropertiesNV);
VULKAN_IDLOAD(vkCmdBeginConditionalRenderingEXT);
VULKAN_IDLOAD(vkCmdEndConditionalRenderingEXT);
VULKAN_IDLOAD(vkCmdSetViewportWScalingNV);
VULKAN_IDLOAD(vkReleaseDisplayEXT);
VULKAN_IDLOAD(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
VULKAN_IDLOAD(vkDisplayPowerControlEXT);
VULKAN_IDLOAD(vkRegisterDeviceEventEXT);
VULKAN_IDLOAD(vkRegisterDisplayEventEXT);
VULKAN_IDLOAD(vkGetSwapchainCounterEXT);
VULKAN_IDLOAD(vkGetRefreshCycleDurationGOOGLE);
VULKAN_IDLOAD(vkGetPastPresentationTimingGOOGLE);
VULKAN_IDLOAD(vkCmdSetDiscardRectangleEXT);
#if VK_HEADER_VERSION >= 241
VULKAN_IDLOAD(vkCmdSetDiscardRectangleEnableEXT);
VULKAN_IDLOAD(vkCmdSetDiscardRectangleModeEXT);
#endif
VULKAN_IDLOAD(vkSetHdrMetadataEXT);
VULKAN_IDLOAD(vkSetDebugUtilsObjectNameEXT);
VULKAN_IDLOAD(vkSetDebugUtilsObjectTagEXT);
VULKAN_IDLOAD(vkQueueBeginDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkQueueEndDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkQueueInsertDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkCmdBeginDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkCmdEndDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkCmdInsertDebugUtilsLabelEXT);
VULKAN_IDLOAD(vkCreateDebugUtilsMessengerEXT);
VULKAN_IDLOAD(vkDestroyDebugUtilsMessengerEXT);
VULKAN_IDLOAD(vkSubmitDebugUtilsMessageEXT);
VULKAN_IDLOAD(vkCmdSetSampleLocationsEXT);
VULKAN_IDLOAD(vkGetPhysicalDeviceMultisamplePropertiesEXT);
VULKAN_IDLOAD(vkCreateValidationCacheEXT);
VULKAN_IDLOAD(vkDestroyValidationCacheEXT);
VULKAN_IDLOAD(vkMergeValidationCachesEXT);
VULKAN_IDLOAD(vkGetValidationCacheDataEXT);
VULKAN_IDLOAD(vkCmdBindShadingRateImageNV);
VULKAN_IDLOAD(vkCmdSetViewportShadingRatePaletteNV);
VULKAN_IDLOAD(vkCmdSetCoarseSampleOrderNV);
VULKAN_IDLOAD(vkCreateAccelerationStructureNV);
VULKAN_IDLOAD(vkDestroyAccelerationStructureNV);
VULKAN_IDLOAD(vkGetAccelerationStructureMemoryRequirementsNV);
VULKAN_IDLOAD(vkBindAccelerationStructureMemoryNV);
VULKAN_IDLOAD(vkCmdBuildAccelerationStructureNV);
VULKAN_IDLOAD(vkCmdCopyAccelerationStructureNV);
VULKAN_IDLOAD(vkCmdTraceRaysNV);
VULKAN_IDLOAD(vkCreateRayTracingPipelinesNV);
VULKAN_IDLOAD(vkGetRayTracingShaderGroupHandlesKHR);
VULKAN_IDLOAD(vkGetRayTracingShaderGroupHandlesNV);
VULKAN_IDLOAD(vkGetAccelerationStructureHandleNV);
VULKAN_IDLOAD(vkCmdWriteAccelerationStructuresPropertiesNV);
VULKAN_IDLOAD(vkCompileDeferredNV);
VULKAN_IDLOAD(vkGetMemoryHostPointerPropertiesEXT);
VULKAN_IDLOAD(vkCmdWriteBufferMarkerAMD);
VULKAN_IDLOAD(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT);
VULKAN_IDLOAD(vkGetCalibratedTimestampsEXT);
VULKAN_IDLOAD(vkCmdDrawMeshTasksNV);
VULKAN_IDLOAD(vkCmdDrawMeshTasksIndirectNV);
VULKAN_IDLOAD(vkCmdDrawMeshTasksIndirectCountNV);
#if VK_HEADER_VERSION >= 241
VULKAN_IDLOAD(vkCmdSetExclusiveScissorEnableNV);
#endif
VULKAN_IDLOAD(vkCmdSetExclusiveScissorNV);
VULKAN_IDLOAD(vkCmdSetCheckpointNV);
VULKAN_IDLOAD(vkGetQueueCheckpointDataNV);
VULKAN_IDLOAD(vkInitializePerformanceApiINTEL);
VULKAN_IDLOAD(vkUninitializePerformanceApiINTEL);
VULKAN_IDLOAD(vkCmdSetPerformanceMarkerINTEL);
VULKAN_IDLOAD(vkCmdSetPerformanceStreamMarkerINTEL);
VULKAN_IDLOAD(vkCmdSetPerformanceOverrideINTEL);
VULKAN_IDLOAD(vkAcquirePerformanceConfigurationINTEL);
VULKAN_IDLOAD(vkReleasePerformanceConfigurationINTEL);
VULKAN_IDLOAD(vkQueueSetPerformanceConfigurationINTEL);
VULKAN_IDLOAD(vkGetPerformanceParameterINTEL);
VULKAN_IDLOAD(vkSetLocalDimmingAMD);
VULKAN_IDLOAD(vkGetBufferDeviceAddressEXT);
VULKAN_IDLOAD(vkGetPhysicalDeviceToolPropertiesEXT);
VULKAN_IDLOAD(vkGetPhysicalDeviceCooperativeMatrixPropertiesNV);
VULKAN_IDLOAD(vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV);
VULKAN_IDLOAD(vkCreateHeadlessSurfaceEXT);
VULKAN_IDLOAD(vkCmdSetLineStippleEXT);
VULKAN_IDLOAD(vkResetQueryPoolEXT);
VULKAN_IDLOAD(vkCmdSetCullModeEXT);
VULKAN_IDLOAD(vkCmdSetFrontFaceEXT);
VULKAN_IDLOAD(vkCmdSetPrimitiveTopologyEXT);
VULKAN_IDLOAD(vkCmdSetViewportWithCountEXT);
VULKAN_IDLOAD(vkCmdSetScissorWithCountEXT);
VULKAN_IDLOAD(vkCmdBindVertexBuffers2EXT);
VULKAN_IDLOAD(vkCmdSetDepthTestEnableEXT);
VULKAN_IDLOAD(vkCmdSetDepthWriteEnableEXT);
VULKAN_IDLOAD(vkCmdSetDepthCompareOpEXT);
VULKAN_IDLOAD(vkCmdSetDepthBoundsTestEnableEXT);
VULKAN_IDLOAD(vkCmdSetStencilTestEnableEXT);
VULKAN_IDLOAD(vkCmdSetStencilOpEXT);
#if VK_HEADER_VERSION >= 258
VULKAN_IDLOAD(vkCopyMemoryToImageEXT);
VULKAN_IDLOAD(vkCopyImageToMemoryEXT);
VULKAN_IDLOAD(vkCopyImageToImageEXT);
VULKAN_IDLOAD(vkTransitionImageLayoutEXT);
#endif
#if VK_HEADER_VERSION >= 213
VULKAN_IDLOAD(vkGetImageSubresourceLayout2EXT);
#endif
#if VK_HEADER_VERSION >= 237
VULKAN_IDLOAD(vkReleaseSwapchainImagesEXT);
#endif
VULKAN_IDLOAD(vkGetGeneratedCommandsMemoryRequirementsNV);
VULKAN_IDLOAD(vkCmdPreprocessGeneratedCommandsNV);
VULKAN_IDLOAD(vkCmdExecuteGeneratedCommandsNV);
VULKAN_IDLOAD(vkCmdBindPipelineShaderGroupNV);
VULKAN_IDLOAD(vkCreateIndirectCommandsLayoutNV);
VULKAN_IDLOAD(vkDestroyIndirectCommandsLayoutNV);
#if VK_HEADER_VERSION >= 254
VULKAN_IDLOAD(vkCmdSetDepthBias2EXT);
#endif
VULKAN_IDLOAD(vkAcquireDrmDisplayEXT);
VULKAN_IDLOAD(vkGetDrmDisplayEXT);
VULKAN_IDLOAD(vkCreatePrivateDataSlotEXT);
VULKAN_IDLOAD(vkDestroyPrivateDataSlotEXT);
VULKAN_IDLOAD(vkSetPrivateDataEXT);
VULKAN_IDLOAD(vkGetPrivateDataEXT);
#if VK_HEADER_VERSION >= 269
VULKAN_IDLOAD(vkCreateCudaModuleNV);
VULKAN_IDLOAD(vkGetCudaModuleCacheNV);
VULKAN_IDLOAD(vkCreateCudaFunctionNV);
VULKAN_IDLOAD(vkDestroyCudaModuleNV);
VULKAN_IDLOAD(vkDestroyCudaFunctionNV);
VULKAN_IDLOAD(vkCmdCudaLaunchKernelNV);
#endif
#if VK_HEADER_VERSION >= 235
VULKAN_IDLOAD(vkGetDescriptorSetLayoutSizeEXT);
VULKAN_IDLOAD(vkGetDescriptorSetLayoutBindingOffsetEXT);
VULKAN_IDLOAD(vkGetDescriptorEXT);
VULKAN_IDLOAD(vkCmdBindDescriptorBuffersEXT);
VULKAN_IDLOAD(vkCmdSetDescriptorBufferOffsetsEXT);
VULKAN_IDLOAD(vkCmdBindDescriptorBufferEmbeddedSamplersEXT);
VULKAN_IDLOAD(vkGetBufferOpaqueCaptureDescriptorDataEXT);
VULKAN_IDLOAD(vkGetImageOpaqueCaptureDescriptorDataEXT);
VULKAN_IDLOAD(vkGetImageViewOpaqueCaptureDescriptorDataEXT);
VULKAN_IDLOAD(vkGetSamplerOpaqueCaptureDescriptorDataEXT);
VULKAN_IDLOAD(vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT);
#endif
VULKAN_IDLOAD(vkCmdSetFragmentShadingRateEnumNV);
#if VK_HEADER_VERSION >= 230
VULKAN_IDLOAD(vkGetDeviceFaultInfoEXT);
#endif
VULKAN_IDLOAD(vkCmdSetVertexInputEXT);
#if VK_HEADER_VERSION >= 184
VULKAN_IDLOAD(vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI);
#else
VULKAN_IDLOAD(vkGetSubpassShadingMaxWorkgroupSizeHUAWEI);
#endif
VULKAN_IDLOAD(vkCmdSubpassShadingHUAWEI);
#if VK_HEADER_VERSION >= 185
VULKAN_IDLOAD(vkCmdBindInvocationMaskHUAWEI);
#endif
#if VK_HEADER_VERSION >= 184
VULKAN_IDLOAD(vkGetMemoryRemoteAddressNV);
#endif
#if VK_HEADER_VERSION >= 213
VULKAN_IDLOAD(vkGetPipelinePropertiesEXT);
#endif
VULKAN_IDLOAD(vkCmdSetPatchControlPointsEXT);
VULKAN_IDLOAD(vkCmdSetRasterizerDiscardEnableEXT);
VULKAN_IDLOAD(vkCmdSetDepthBiasEnableEXT);
VULKAN_IDLOAD(vkCmdSetLogicOpEXT);
VULKAN_IDLOAD(vkCmdSetPrimitiveRestartEnableEXT);
VULKAN_IDLOAD(vkCmdSetColorWriteEnableEXT);
VULKAN_IDLOAD(vkCmdDrawMultiEXT);
VULKAN_IDLOAD(vkCmdDrawMultiIndexedEXT);
#if VK_HEADER_VERSION >= 230
VULKAN_IDLOAD(vkCreateMicromapEXT);
VULKAN_IDLOAD(vkDestroyMicromapEXT);
VULKAN_IDLOAD(vkCmdBuildMicromapsEXT);
VULKAN_IDLOAD(vkBuildMicromapsEXT);
VULKAN_IDLOAD(vkCopyMicromapEXT);
VULKAN_IDLOAD(vkCopyMicromapToMemoryEXT);
VULKAN_IDLOAD(vkCopyMemoryToMicromapEXT);
VULKAN_IDLOAD(vkWriteMicromapsPropertiesEXT);
VULKAN_IDLOAD(vkCmdCopyMicromapEXT);
VULKAN_IDLOAD(vkCmdCopyMicromapToMemoryEXT);
VULKAN_IDLOAD(vkCmdCopyMemoryToMicromapEXT);
VULKAN_IDLOAD(vkCmdWriteMicromapsPropertiesEXT);
VULKAN_IDLOAD(vkGetDeviceMicromapCompatibilityEXT);
VULKAN_IDLOAD(vkGetMicromapBuildSizesEXT);
#endif
#if VK_HEADER_VERSION >= 239
VULKAN_IDLOAD(vkCmdDrawClusterHUAWEI);
VULKAN_IDLOAD(vkCmdDrawClusterIndirectHUAWEI);
#endif
#if VK_HEADER_VERSION >= 191
VULKAN_IDLOAD(vkSetDeviceMemoryPriorityEXT);
#endif
#if VK_HEADER_VERSION >= 207
VULKAN_IDLOAD(vkGetDescriptorSetLayoutHostMappingInfoVALVE);
VULKAN_IDLOAD(vkGetDescriptorSetHostMappingVALVE);
#endif
#if VK_HEADER_VERSION >= 233
VULKAN_IDLOAD(vkCmdCopyMemoryIndirectNV);
VULKAN_IDLOAD(vkCmdCopyMemoryToImageIndirectNV);
VULKAN_IDLOAD(vkCmdDecompressMemoryNV);
VULKAN_IDLOAD(vkCmdDecompressMemoryIndirectCountNV);
#endif
#if VK_HEADER_VERSION >= 258
VULKAN_IDLOAD(vkGetPipelineIndirectMemoryRequirementsNV);
#endif
#if VK_HEADER_VERSION == 258
VULKAN_IDLOAD(vkCmdUpdatePipelineIndirectBuffer);
#endif
#if VK_HEADER_VERSION >= 259
VULKAN_IDLOAD(vkCmdUpdatePipelineIndirectBufferNV);
#endif
#if VK_HEADER_VERSION >= 258
VULKAN_IDLOAD(vkGetPipelineIndirectDeviceAddressNV);
#endif
#if VK_HEADER_VERSION >= 230
VULKAN_IDLOAD(vkCmdSetDepthClampEnableEXT);
VULKAN_IDLOAD(vkCmdSetPolygonModeEXT);
VULKAN_IDLOAD(vkCmdSetRasterizationSamplesEXT);
VULKAN_IDLOAD(vkCmdSetSampleMaskEXT);
VULKAN_IDLOAD(vkCmdSetAlphaToCoverageEnableEXT);
VULKAN_IDLOAD(vkCmdSetAlphaToOneEnableEXT);
VULKAN_IDLOAD(vkCmdSetLogicOpEnableEXT);
VULKAN_IDLOAD(vkCmdSetColorBlendEnableEXT);
VULKAN_IDLOAD(vkCmdSetColorBlendEquationEXT);
VULKAN_IDLOAD(vkCmdSetColorWriteMaskEXT);
VULKAN_IDLOAD(vkCmdSetTessellationDomainOriginEXT);
VULKAN_IDLOAD(vkCmdSetRasterizationStreamEXT);
VULKAN_IDLOAD(vkCmdSetConservativeRasterizationModeEXT);
VULKAN_IDLOAD(vkCmdSetExtraPrimitiveOverestimationSizeEXT);
VULKAN_IDLOAD(vkCmdSetDepthClipEnableEXT);
VULKAN_IDLOAD(vkCmdSetSampleLocationsEnableEXT);
VULKAN_IDLOAD(vkCmdSetColorBlendAdvancedEXT);
VULKAN_IDLOAD(vkCmdSetProvokingVertexModeEXT);
VULKAN_IDLOAD(vkCmdSetLineRasterizationModeEXT);
VULKAN_IDLOAD(vkCmdSetLineStippleEnableEXT);
VULKAN_IDLOAD(vkCmdSetDepthClipNegativeOneToOneEXT);
VULKAN_IDLOAD(vkCmdSetViewportWScalingEnableNV);
VULKAN_IDLOAD(vkCmdSetViewportSwizzleNV);
VULKAN_IDLOAD(vkCmdSetCoverageToColorEnableNV);
VULKAN_IDLOAD(vkCmdSetCoverageToColorLocationNV);
VULKAN_IDLOAD(vkCmdSetCoverageModulationModeNV);
VULKAN_IDLOAD(vkCmdSetCoverageModulationTableEnableNV);
VULKAN_IDLOAD(vkCmdSetCoverageModulationTableNV);
VULKAN_IDLOAD(vkCmdSetShadingRateImageEnableNV);
VULKAN_IDLOAD(vkCmdSetRepresentativeFragmentTestEnableNV);
VULKAN_IDLOAD(vkCmdSetCoverageReductionModeNV);
#endif
#if VK_HEADER_VERSION >= 219
VULKAN_IDLOAD(vkGetShaderModuleIdentifierEXT);
VULKAN_IDLOAD(vkGetShaderModuleCreateInfoIdentifierEXT);
#endif
#if VK_HEADER_VERSION >= 230
VULKAN_IDLOAD(vkGetPhysicalDeviceOpticalFlowImageFormatsNV);
VULKAN_IDLOAD(vkCreateOpticalFlowSessionNV);
VULKAN_IDLOAD(vkDestroyOpticalFlowSessionNV);
VULKAN_IDLOAD(vkBindOpticalFlowSessionImageNV);
VULKAN_IDLOAD(vkCmdOpticalFlowExecuteNV);
#endif
#if VK_HEADER_VERSION >= 246
VULKAN_IDLOAD(vkCreateShadersEXT);
VULKAN_IDLOAD(vkDestroyShaderEXT);
VULKAN_IDLOAD(vkGetShaderBinaryDataEXT);
VULKAN_IDLOAD(vkCmdBindShadersEXT);
#endif
#if VK_HEADER_VERSION >= 222
VULKAN_IDLOAD(vkGetFramebufferTilePropertiesQCOM);
VULKAN_IDLOAD(vkGetDynamicRenderingTilePropertiesQCOM);
#endif
#if VK_HEADER_VERSION >= 266
VULKAN_IDLOAD(vkSetLatencySleepModeNV);
VULKAN_IDLOAD(vkLatencySleepNV);
VULKAN_IDLOAD(vkSetLatencyMarkerNV);
VULKAN_IDLOAD(vkGetLatencyTimingsNV);
VULKAN_IDLOAD(vkQueueNotifyOutOfBandNV);
#endif
#if VK_HEADER_VERSION >= 250
VULKAN_IDLOAD(vkCmdSetAttachmentFeedbackLoopEnableEXT);
#endif
VULKAN_IDLOAD(vkCreateAccelerationStructureKHR);
VULKAN_IDLOAD(vkDestroyAccelerationStructureKHR);
VULKAN_IDLOAD(vkCmdBuildAccelerationStructuresKHR);
VULKAN_IDLOAD(vkCmdBuildAccelerationStructuresIndirectKHR);
VULKAN_IDLOAD(vkBuildAccelerationStructuresKHR);
VULKAN_IDLOAD(vkCopyAccelerationStructureKHR);
VULKAN_IDLOAD(vkCopyAccelerationStructureToMemoryKHR);
VULKAN_IDLOAD(vkCopyMemoryToAccelerationStructureKHR);
VULKAN_IDLOAD(vkWriteAccelerationStructuresPropertiesKHR);
VULKAN_IDLOAD(vkCmdCopyAccelerationStructureKHR);
VULKAN_IDLOAD(vkCmdCopyAccelerationStructureToMemoryKHR);
VULKAN_IDLOAD(vkCmdCopyMemoryToAccelerationStructureKHR);
VULKAN_IDLOAD(vkGetAccelerationStructureDeviceAddressKHR);
VULKAN_IDLOAD(vkCmdWriteAccelerationStructuresPropertiesKHR);
VULKAN_IDLOAD(vkGetDeviceAccelerationStructureCompatibilityKHR);
VULKAN_IDLOAD(vkGetAccelerationStructureBuildSizesKHR);
VULKAN_IDLOAD(vkCmdTraceRaysKHR);
VULKAN_IDLOAD(vkCreateRayTracingPipelinesKHR);
VULKAN_IDLOAD(vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
VULKAN_IDLOAD(vkCmdTraceRaysIndirectKHR);
VULKAN_IDLOAD(vkGetRayTracingShaderGroupStackSizeKHR);
VULKAN_IDLOAD(vkCmdSetRayTracingPipelineStackSizeKHR);
#if VK_HEADER_VERSION >= 226
VULKAN_IDLOAD(vkCmdDrawMeshTasksEXT);
VULKAN_IDLOAD(vkCmdDrawMeshTasksIndirectEXT);
VULKAN_IDLOAD(vkCmdDrawMeshTasksIndirectCountEXT);
#endif

// vim:ts=4:sw=4:noexpandtab
