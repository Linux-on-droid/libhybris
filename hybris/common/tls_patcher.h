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

#ifndef TLS_PATCHER_H
#define TLS_PATCHER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hybris_tls_patcher_t)(void* segment_addr, size_t segment_size, const char* library_name);
typedef void (*hybris_register_thunk_region_t)(void* start, size_t size);
typedef void* (*hybris_allocate_thunk_near_t)(void* target_addr, size_t thunk_size);
typedef int (*hybris_is_within_thunk_region_t)(void* addr);

/* Struct containing all TLS patcher function pointers */
struct hybris_tls_patcher_funcs {
    hybris_tls_patcher_t patch_tls;
    hybris_register_thunk_region_t register_thunk_region;
    hybris_allocate_thunk_near_t allocate_thunk_near;
    hybris_is_within_thunk_region_t is_within_thunk_region;
};
typedef struct hybris_tls_patcher_funcs hybris_tls_patcher_funcs_t;

/* Register a thunk region allocated by the linker */
void hybris_register_thunk_region(void* start, size_t size);

/* Allocate a thunk near the target address */
void* hybris_allocate_thunk_near(void* target_addr, size_t thunk_size);

/* Check if an address is within a thunk region */
int hybris_is_within_thunk_region(void* addr);

/* Patches TLS accesses in the given segment at runtime */
void hybris_patch_tls(void* segment_addr, size_t segment_size, const char* library_name);

#ifdef __cplusplus
}
#endif

#endif /* TLS_PATCHER_H */
