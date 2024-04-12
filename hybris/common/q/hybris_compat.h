/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef HYBRIS_ANDROID_MM_COMPAT_H_
#define HYBRIS_ANDROID_MM_COMPAT_H_

#include <string.h>
#include <memory.h>

extern "C" size_t strlcpy(char *dest, const char *src, size_t size);
extern "C" size_t strlcat(char *dst, const char *src, size_t size);

#define ELF_ST_BIND(x)          ((x) >> 4)

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))

/*
 * From bionic/libc/include/elf.h
 *
 * Experimental support for SHT_RELR sections. For details, see proposal
 * at https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
 */
#define DT_ANDROID_RELR 0x6fffe000
#define DT_ANDROID_RELRSZ 0x6fffe001
#define DT_ANDROID_RELRENT 0x6fffe003
#define DT_ANDROID_RELRCOUNT 0x6fffe005

/* Defined in glibc >= 2.36, and used in bionic since apilevel 30 */
#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif

#ifndef DT_RELR
#define DT_RELR 36
#endif

#ifndef DT_RELRENT
#define DT_RELRENT 37
#endif

/*
 * From bionic/libc/include/elf.h
 *
 * Android compressed rel/rela sections
 */
#define DT_ANDROID_REL (DT_LOOS + 2)
#define DT_ANDROID_RELSZ (DT_LOOS + 3)

#define DT_ANDROID_RELA (DT_LOOS + 4)
#define DT_ANDROID_RELASZ (DT_LOOS + 5)

/*
 * From bionic/libc/include/bits/elf_arm64.h
 */
#define R_AARCH64_TLS_DTPREL64          1028    /* Module-relative offset. */
#define R_AARCH64_TLS_DTPMOD64          1029    /* Module index. */
#define R_AARCH64_TLS_TPREL64           1030    /* TP-relative offset. */

/*
 * From bionic/libc/include/bits/elf_arm64.h
 *
 * Dynamic array tags
 */
#define DT_AARCH64_BTI_PLT              0x70000001
#define DT_AARCH64_PAC_PLT              0x70000003
#define DT_AARCH64_VARIANT_PCS          0x70000005

#endif
