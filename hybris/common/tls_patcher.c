/*
 * Copyright (c) 2025 Nikita Ukhrenkov <thekit@disroot.org>
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

#include "tls_patcher.h"
#include "logging.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>

extern void* _hybris_hook___get_tls_hooks(void);

/* Architecture-specific patch function */
extern void hybris_patch_tls_arch(void* segment_addr, size_t segment_size, int tls_offset);

/* Simple thunk region tracking */
#define MAX_THUNK_REGIONS 32
static struct {
    void* start;
    size_t size;
    size_t used;
} thunk_regions[MAX_THUNK_REGIONS];
static int num_thunk_regions = 0;
static pthread_mutex_t thunk_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Register a thunk region allocated by the linker */
void hybris_register_thunk_region(void* start, size_t size) {
    pthread_mutex_lock(&thunk_mutex);

    if (num_thunk_regions < MAX_THUNK_REGIONS) {
        thunk_regions[num_thunk_regions].start = start;
        thunk_regions[num_thunk_regions].size = size;
        thunk_regions[num_thunk_regions].used = 0;
        num_thunk_regions++;

        HYBRIS_DEBUG_LOG(HOOKS, "Registered thunk region: %p - %p (%zu bytes)",
                        start, (char*)start + size, size);
    }

    pthread_mutex_unlock(&thunk_mutex);
}

/* Allocate a thunk near the target address */
void* hybris_allocate_thunk_near(void* target_addr, size_t thunk_size) {
    pthread_mutex_lock(&thunk_mutex);

    uintptr_t target = (uintptr_t)target_addr;
    void* result = NULL;

    /* Find the closest thunk region within branching distance (128MB for ARM64) */
    for (int i = 0; i < num_thunk_regions; i++) {
        uintptr_t region_addr = (uintptr_t)thunk_regions[i].start;
        uintptr_t distance = (target > region_addr) ? (target - region_addr) : (region_addr - target);

        if (distance > (128 * 1024 * 1024)) {
            continue;
        }

        /* Align thunk size to 16-byte boundary */
        size_t aligned_size = (thunk_size + 15) & ~15;

        if (thunk_regions[i].used + aligned_size <= thunk_regions[i].size) {
            result = (char*)thunk_regions[i].start + thunk_regions[i].used;
            thunk_regions[i].used += aligned_size;

            HYBRIS_DEBUG_LOG(HOOKS, "Allocated thunk at %p (size %zu) for target %p",
                           result, thunk_size, target_addr);
            break;
        }
    }

    if (!result) {
        HYBRIS_DEBUG_LOG(HOOKS, "Failed to allocate thunk for target %p - no suitable region found", target_addr);
    }

    pthread_mutex_unlock(&thunk_mutex);
    return result;
}

/* Check if an address is within a thunk region */
int hybris_is_within_thunk_region(void* addr) {
    pthread_mutex_lock(&thunk_mutex);

    uintptr_t check_addr = (uintptr_t)addr;
    int result = 0;

    for (int i = 0; i < num_thunk_regions; i++) {
        uintptr_t region_start = (uintptr_t)thunk_regions[i].start;
        uintptr_t region_end = region_start + thunk_regions[i].size;

        if (check_addr >= region_start && check_addr < region_end) {
            result = 1;
            break;
        }
    }

    pthread_mutex_unlock(&thunk_mutex);
    return result;
}

/* Calculate offset from thread pointer to our TLS area */
static int hybris_calculate_tls_offset(void) {
    void* tp = __builtin_thread_pointer();
    void* tls_area = _hybris_hook___get_tls_hooks();
    return (int)((uintptr_t)tls_area - (uintptr_t)tp) / sizeof(uintptr_t);
}

/* Extract filename from path without modifying input */
static const char* get_basename(const char* path) {
    const char* base = strrchr(path, '/');
    return base ? base + 1 : path;
}

/* Check if library should be patched based on HYBRIS_PATCH_TLS value */
static int should_patch_library(const char* library_name) {
    static const char* patch_tls = NULL;
    static int init_done = 0;

    /* Initialize on first call */
    if (!init_done) {
        patch_tls = getenv("HYBRIS_PATCH_TLS");
        init_done = 1;
    }

    /* No environment variable set - do nothing */
    if (!patch_tls) {
        return 0;
    }

    /* Simple enable/disable */
    if (patch_tls[0] == '0' || patch_tls[0] == '1') {
        return patch_tls[0] == '1';
    }

    /* Check if library basename is in colon-separated list */
    const char *start = patch_tls;
    const char *name = get_basename(library_name);
    size_t name_len = strlen(name);

    while (start && *start) {
        const char *end = strchr(start, ':');
        size_t len = end ? (size_t)(end - start) : strlen(start);

        if (len == name_len && strncmp(start, name, len) == 0) {
            return 1;
        }

        start = end ? end + 1 : NULL;
    }

    return 0;
}

void hybris_patch_tls(void* segment_addr, size_t segment_size, const char* library_name) {
    if (!should_patch_library(library_name)) {
        return;
    }

    HYBRIS_DEBUG_LOG(HOOKS, "Patching TLS accesses in %s", library_name);

    int tls_offset = hybris_calculate_tls_offset();
    HYBRIS_DEBUG_LOG(HOOKS, "Offset from thread pointer to hybris tls_space (in words): %d",
        tls_offset);

    hybris_patch_tls_arch(segment_addr, segment_size, tls_offset);
}

#ifdef __aarch64__
#include "tls_patcher_aarch64.c"
#endif
